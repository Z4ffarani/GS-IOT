// Tela do tópico: consulta o estado a cada 2 s e redesenha. Textos entram
// sempre por textContent (nunca innerHTML) para não executar nada vindo da API.
const TOPICO = location.pathname.split("/").filter(Boolean).pop() || "travessia-01";
const PUBLICO = { deficiencia_visual: "Deficiência visual", deficiencia_auditiva: "Deficiência auditiva" };
const vistos = new Set();
let primeiraCarga = true;

const $ = (id) => document.getElementById(id);
const el = (tag, cls, texto) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (texto !== undefined) e.textContent = texto;
  return e;
};

const hora = (iso) =>
  new Date(iso).toLocaleTimeString("pt-BR", { timeZone: "America/Sao_Paulo", hour: "2-digit", minute: "2-digit", second: "2-digit" });

function haQuanto(iso, agoraIso) {
  const s = Math.max(0, Math.round((new Date(agoraIso) - new Date(iso)) / 1000));
  if (s < 60) return `há ${s} s`;
  const m = Math.floor(s / 60);
  return `há ${m} min`;
}

function duracao(s) {
  if (s < 60) return `${s} s`;
  const m = Math.floor(s / 60), r = s % 60;
  return r ? `${m} min ${r} s` : `${m} min`;
}

function desenharSituacao(d) {
  const caixa = $("situacao");
  caixa.className = "situacao " + (d.situacao === "falha" ? "falha" : d.situacao === "ok" ? "ok" : "");
  if (d.situacao === "falha") {
    $("situacao-titulo").textContent = `Barreira ativa: ${d.em_falha.join(", ").toLowerCase()} sem funcionar`;
    $("situacao-texto").textContent = "Detectado pelo AJA Sentinela e anexado ao dossiê do tópico.";
  } else if (d.situacao === "ok") {
    $("situacao-titulo").textContent = "Equipamento funcionando";
    $("situacao-texto").textContent = d.impacto.falhas_confirmadas
      ? "O dispositivo confirmou o conserto e segue verificando se ele se mantém."
      : "Monitorado continuamente pelo AJA Sentinela.";
  } else {
    $("situacao-titulo").textContent = "Aguardando o equipamento";
    $("situacao-texto").textContent = "Nenhum aviso recebido ainda.";
  }
}

function itemEvidencia(ev) {
  const li = el("li", "item " + ev.tipo);
  const topo = el("div", "item-topo");
  const origem = ev.tipo === "relato" ? "Relato da comunidade" : "Evidência automática · IoT";
  topo.append(el("span", "origem", origem), el("span", "", ev.quando ? hora(ev.quando) : ""));
  li.append(topo, el("h3", "", ev.titulo), el("p", "", ev.texto));
  if (ev.publico && ev.publico.length) {
    const tags = el("div", "tags");
    ev.publico.forEach((p) => tags.append(el("span", "", PUBLICO[p] || p)));
    li.append(tags);
  }
  return li;
}

function desenharEvidencias(d) {
  const lista = $("linha-tempo");
  const relatos = d.topico.relatos.map((r, i) => ({
    id: "r" + i, tipo: "relato", quando: null,
    titulo: `${r.autor}`, texto: `“${r.texto}” · ${r.confirmacoes} confirmações`, publico: [],
  }));
  const todos = [...d.evidencias, ...relatos];
  lista.replaceChildren();
  let anuncio = "";
  todos.forEach((ev) => {
    const li = itemEvidencia(ev);
    if (!vistos.has(ev.id)) {
      vistos.add(ev.id);
      if (!primeiraCarga) {
        li.classList.add("novo");
        anuncio = anuncio || `Nova evidência: ${ev.titulo}`;
      }
    }
    lista.append(li);
  });
  if (anuncio) $("anuncio").textContent = anuncio;
}

function desenharDispositivo(d) {
  const disp = d.dispositivo;
  const online = disp.ultimo_contato && (new Date(d.agora) - new Date(disp.ultimo_contato)) / 1000 < 150;
  const selo = $("conexao");
  selo.className = "selo" + (online ? " on" : "");
  selo.textContent = online ? "Conectado" : "Sem contato";
  $("disp-desc").textContent = disp.ultimo_contato
    ? `${disp.equipamento} · último contato ${haQuanto(disp.ultimo_contato, d.agora)}`
    : `${disp.equipamento} · aguardando o primeiro contato`;

  const ul = $("componentes");
  ul.replaceChildren();
  disp.componentes.forEach((c) => {
    const li = el("li");
    li.append(el("span", "", c.nome), el("span", "estado " + (c.em_falha ? "falha" : "ok"), c.em_falha ? "Falha" : "Funcionando"));
    ul.append(li);
  });

  const r = disp.ultimo_relatorio;
  $("relatorio").hidden = !r;
  if (r) {
    $("r-acionamentos").textContent = r.acionamentos;
    $("r-ciclos").textContent = r.ciclos;
    const barras = $("r-disp");
    barras.replaceChildren();
    Object.entries(r.disponibilidade).forEach(([nome, pct]) => {
      const li = el("li");
      const rot = el("div", "rotulo");
      rot.append(el("span", "", nome), el("span", "valor", `${pct.toLocaleString("pt-BR")}%`));
      const trilho = el("div", "trilho");
      const cheio = el("div", "cheio");
      cheio.style.width = `${Math.max(0, Math.min(100, pct))}%`;
      trilho.append(cheio);
      li.append(rot, trilho);
      barras.append(li);
    });
  }
}

function desenhar(d) {
  $("titulo").textContent = d.topico.titulo;
  $("local").textContent = d.topico.local;
  $("categoria").textContent = d.topico.categoria;
  const pub = $("publico");
  pub.replaceChildren(...d.topico.publico.map((p) => el("li", "", p)));

  desenharSituacao(d);
  desenharEvidencias(d);
  desenharDispositivo(d);

  $("i-acionamentos").textContent = d.impacto.acionamentos_durante_falhas;
  $("i-tempo").textContent = duracao(d.impacto.segundos_parado);
  $("i-falhas").textContent = d.impacto.falhas_confirmadas;
  $("d-orgao").textContent = d.dossie.orgao;
  $("d-equip").textContent = d.dossie.evidencias_equipamento;
  $("i-dossie").textContent = d.dossie.evidencias_equipamento;
  $("d-relatos").textContent = d.dossie.relatos;
  primeiraCarga = false;
}

async function atualizar() {
  try {
    const resp = await fetch(`/api/v1/topicos/${TOPICO}`, { cache: "no-store" });
    if (resp.ok) desenhar(await resp.json());
  } catch (_) {
    // sem conexão: mantém a última tela e tenta de novo
  }
}

atualizar();
setInterval(atualizar, 2000);

// Reinício da demonstração (para regravar o vídeo): só aparece com
// #reiniciar=CHAVE no endereço; a chave não vai para o servidor na URL.
const chaveAdmin = new URLSearchParams(location.hash.slice(1)).get("reiniciar");
if (chaveAdmin) {
  $("admin").hidden = false;
  $("reiniciar").addEventListener("click", async () => {
    const r = await fetch("/api/v1/demo/reiniciar", { method: "POST", headers: { "X-AJA-Admin-Key": chaveAdmin } });
    if (r.ok) { vistos.clear(); primeiraCarga = true; atualizar(); }
  });
}
