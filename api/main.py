"""API do AJA para o AJA Sentinela.

Recebe os eventos do dispositivo IoT instalado na travessia e os anexa como
evidência ao tópico correspondente. A tela do tópico consulta o estado a cada
poucos segundos, então um aviso enviado pelo ESP32 aparece na hora.
"""
import hmac
import json
import os
import sqlite3
from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path
from typing import Literal, Optional

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import FileResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, ConfigDict

BASE = Path(__file__).parent
DB_PATH = os.environ.get("AJA_DB", str(BASE / "aja.db"))
CHAVE_DISPOSITIVO = os.environ.get("AJA_DEVICE_KEY", "chave-demo-troque-em-producao")
CHAVE_ADMIN = os.environ.get("AJA_ADMIN_KEY", "")
LIMITE_CORPO = 4096

# Cadastro do dispositivo: a localização e o tópico ficam aqui, não no evento.
DISPOSITIVOS = {
    "AJA-PDOR-TRAV-001": {
        "topico": "travessia-01",
        "nome": "AJA Sentinela 001",
        "equipamento": "Semáforo sonoro com botoeira",
    }
}

TOPICOS = {
    "travessia-01": {
        "id": "travessia-01",
        "titulo": "Semáforo sonoro da travessia monitorada 01",
        "local": "Parque Doroteia, São Paulo/SP",
        "categoria": "Acessibilidade · Travessias",
        "publico": ["Pessoas com deficiência visual", "Pessoas com deficiência auditiva"],
        "orgao": "Subprefeitura Cidade Ademar",
        "relatos": [
            {
                "autor": "Morador(a) A",
                "texto": "O semáforo da travessia às vezes para de apitar e eu só percebo quando chego na esquina.",
                "confirmacoes": 4,
            },
            {
                "autor": "Morador(a) B",
                "texto": "Quando a luz verde falha, não tenho como saber que posso atravessar.",
                "confirmacoes": 2,
            },
        ],
    }
}

COMPONENTES = {
    "SINAL_SONORO": "Sinal sonoro",
    "LAMPADA_VERDE_PEDESTRE": "Luz verde de pedestre",
    "BOTOEIRA": "Botoeira",
}
# O dispositivo envia códigos; o texto mostrado ao cidadão fica na plataforma.
DESCRICAO_FALHA = {
    "SINAL_SONORO": "Sem o som, pessoas cegas ou com baixa visão não sabem quando é seguro atravessar.",
    "LAMPADA_VERDE_PEDESTRE": "Sem a luz verde, pessoas surdas não têm o sinal visual para atravessar.",
    "BOTOEIRA": "Com a botoeira travada, novos pedidos de travessia não são registrados.",
}


class Evento(BaseModel):
    model_config = ConfigDict(extra="allow")
    dispositivo_id: str
    tipo: Literal["DISPOSITIVO_INICIADO", "FALHA_DETECTADA", "FALHA_RESOLVIDA", "RELATORIO_PERIODICO"]
    seq: int
    uptime_s: int
    ts: Optional[str] = None
    componente: Optional[str] = None


def conectar():
    con = sqlite3.connect(DB_PATH)
    con.row_factory = sqlite3.Row
    return con


def criar_banco():
    Path(DB_PATH).parent.mkdir(parents=True, exist_ok=True)
    with closing(conectar()) as con, con:
        con.execute(
            """CREATE TABLE IF NOT EXISTS eventos (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                dispositivo_id TEXT NOT NULL,
                tipo TEXT NOT NULL,
                componente TEXT,
                seq INTEGER NOT NULL,
                uptime_s INTEGER NOT NULL,
                ts_dispositivo TEXT,
                recebido_em TEXT NOT NULL,
                corpo TEXT NOT NULL,
                UNIQUE (dispositivo_id, tipo, seq, uptime_s)
            )"""
        )


app = FastAPI(
    title="AJA · API de dispositivos",
    description="Recebe os eventos do AJA Sentinela e os anexa como evidência ao tópico da travessia.",
    version="1.0.0",
)
app.mount("/static", StaticFiles(directory=BASE / "static"), name="static")
criar_banco()


@app.post("/api/v1/dispositivos/eventos", status_code=201)
async def receber_evento(
    request: Request,
    x_aja_device_id: str = Header(...),
    x_aja_device_key: str = Header(...),
):
    corpo = await request.body()
    if len(corpo) > LIMITE_CORPO:
        raise HTTPException(413, "Evento grande demais")

    if x_aja_device_id not in DISPOSITIVOS or not hmac.compare_digest(x_aja_device_key, CHAVE_DISPOSITIVO):
        raise HTTPException(401, "Dispositivo não autorizado")

    try:
        evento = Evento.model_validate_json(corpo)
    except Exception:
        raise HTTPException(422, "Evento fora do formato esperado")
    if evento.dispositivo_id != x_aja_device_id:
        raise HTTPException(401, "Identificador do corpo diferente do cabeçalho")

    agora = datetime.now(timezone.utc).isoformat()
    with closing(conectar()) as con, con:
        cur = con.execute(
            """INSERT OR IGNORE INTO eventos
               (dispositivo_id, tipo, componente, seq, uptime_s, ts_dispositivo, recebido_em, corpo)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            (evento.dispositivo_id, evento.tipo, evento.componente, evento.seq, evento.uptime_s,
             evento.ts, agora, corpo.decode()),
        )
    duplicado = cur.rowcount == 0
    return {"recebido": True, "duplicado": duplicado, "topico": DISPOSITIVOS[x_aja_device_id]["topico"]}


def montar_topico(topico_id: str) -> dict:
    topico = TOPICOS.get(topico_id)
    if not topico:
        raise HTTPException(404, "Tópico não encontrado")
    disp_id, disp = next((k, v) for k, v in DISPOSITIVOS.items() if v["topico"] == topico_id)

    with closing(conectar()) as con:
        linhas = con.execute(
            "SELECT * FROM eventos WHERE dispositivo_id = ? ORDER BY id", (disp_id,)
        ).fetchall()

    estado = {c: {"nome": n, "em_falha": False, "desde": None} for c, n in COMPONENTES.items()}
    evidencias, ultimo_relatorio = [], None
    acionamentos_em_falha, segundos_parado, falhas_confirmadas = 0, 0, 0

    for linha in linhas:
        corpo = json.loads(linha["corpo"])
        quando = linha["recebido_em"]
        comp = linha["componente"]
        nome = COMPONENTES.get(comp, comp)

        if linha["tipo"] == "FALHA_DETECTADA" and comp in estado:
            estado[comp].update(em_falha=True, desde=quando)
            falhas_confirmadas += 1
            evidencias.append({
                "id": linha["id"], "quando": quando, "tipo": "falha",
                "titulo": f"{nome}: falha detectada pelo equipamento",
                "texto": DESCRICAO_FALHA.get(comp, corpo.get("descricao", "")),
                "publico": corpo.get("publico_afetado", []),
            })
        elif linha["tipo"] == "FALHA_RESOLVIDA" and comp in estado:
            estado[comp].update(em_falha=False, desde=None)
            parado = int(corpo.get("tempo_inoperante_s", 0))
            usos = int(corpo.get("acionamentos_durante_falha", 0))
            segundos_parado += parado
            acionamentos_em_falha += usos
            evidencias.append({
                "id": linha["id"], "quando": quando, "tipo": "resolvida",
                "titulo": f"{nome}: voltou a funcionar",
                "texto": f"Ficou {parado} s sem funcionar. A botoeira foi acionada {usos} "
                         f"{'vez' if usos == 1 else 'vezes'} durante a falha.",
                "publico": corpo.get("publico_afetado", []),
            })
        elif linha["tipo"] == "RELATORIO_PERIODICO":
            ultimo_relatorio = {
                "quando": quando,
                "janela_s": corpo.get("janela_s"),
                "acionamentos": corpo.get("acionamentos_botoeira"),
                "ciclos": corpo.get("ciclos_travessia"),
                "disponibilidade": {
                    COMPONENTES.get(k, k): v for k, v in (corpo.get("disponibilidade_pct") or {}).items()
                },
            }
        elif linha["tipo"] == "DISPOSITIVO_INICIADO":
            evidencias.append({
                "id": linha["id"], "quando": quando, "tipo": "info",
                "titulo": "Equipamento conectado ao AJA",
                "texto": "O AJA Sentinela passou a monitorar o som, a luz verde e a botoeira desta travessia.",
                "publico": [],
            })

    ultimo_contato = linhas[-1]["recebido_em"] if linhas else None
    em_falha = [v["nome"] for v in estado.values() if v["em_falha"]]
    return {
        "topico": {k: v for k, v in topico.items()},
        "dispositivo": {
            "id": disp_id, **disp,
            "ultimo_contato": ultimo_contato,
            "componentes": list(estado.values()),
            "ultimo_relatorio": ultimo_relatorio,
        },
        "situacao": "falha" if em_falha else ("ok" if linhas else "sem_dados"),
        "em_falha": em_falha,
        "impacto": {
            "falhas_confirmadas": falhas_confirmadas,
            "acionamentos_durante_falhas": acionamentos_em_falha,
            "segundos_parado": segundos_parado,
        },
        "dossie": {
            "orgao": topico["orgao"],
            "evidencias_equipamento": sum(1 for e in evidencias if e["tipo"] in ("falha", "resolvida")),
            "relatos": len(topico["relatos"]),
        },
        "evidencias": list(reversed(evidencias)),
        "agora": datetime.now(timezone.utc).isoformat(),
    }


@app.get("/api/v1/topicos/{topico_id}")
def consultar_topico(topico_id: str):
    return montar_topico(topico_id)


@app.post("/api/v1/demo/reiniciar")
def reiniciar_demo(x_aja_admin_key: str = Header(...)):
    if not CHAVE_ADMIN or not hmac.compare_digest(x_aja_admin_key, CHAVE_ADMIN):
        raise HTTPException(401, "Não autorizado")
    with closing(conectar()) as con, con:
        con.execute("DELETE FROM eventos")
    return {"reiniciado": True}


@app.get("/saude")
def saude():
    return {"ok": True}


@app.get("/", include_in_schema=False)
def inicio():
    return RedirectResponse("/topicos/travessia-01")


@app.get("/topicos/{topico_id}", include_in_schema=False)
def pagina_topico(topico_id: str):
    if topico_id not in TOPICOS:
        raise HTTPException(404, "Tópico não encontrado")
    return FileResponse(BASE / "static" / "topico.html")
