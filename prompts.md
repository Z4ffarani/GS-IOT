# Prompts usados com IA

Registro do uso de IA como copiloto nesta entrega de Physical Computing (IoT & IoB), como pede o briefing da Global Solution 2026.2 (CitizenTech). O desenvolvimento foi híbrido: equipe e ferramenta trabalharam juntas, em idas e vindas, então as etapas não separam com exatidão o que veio de cada lado. Os objetivos estão resumidos por etapa, na ordem do trabalho, com o resultado alcançado. As decisões de escopo, revisão e aprovação foram da equipe.

---

## 1. Entender a tarefa

**Objetivo:** ler o enunciado de Physical Computing e o material do AJA antes de implementar, e confirmar se uma API ligada ao circuito ficaria conectada à plataforma, já que o circuito não pode ser um projeto paralelo.

**Resultado:** os requisitos ficaram claros: projeto ESP32 no Wokwi com link, vídeo de até 4 minutos e arquivo com os integrantes, com o circuito alimentando o AJA. A equipe decidiu avançar por partes.

**Objetivo:** definir o que desenvolver.

**Resultado:** a equipe escolheu o AJA Sentinela, um ESP32 dentro do semáforo de pedestre. Ele opera a travessia acessível, verifica se o som, a luz verde e a botoeira funcionam e envia as falhas ao tópico da travessia no AJA.

## 2. Planejar o escopo e a ligação com o AJA

**Objetivo:** dar destaque à ligação entre o hardware e o AJA e à premissa da plataforma, a partir da ideia da equipe de reportar mau funcionamento e contar quantas pessoas precisaram do equipamento durante a falha.

**Resultado:** foram definidos quatro eventos:
- `DISPOSITIVO_INICIADO`;
- `FALHA_DETECTADA`;
- `FALHA_RESOLVIDA`, com `tempo_inoperante_s` e `acionamentos_durante_falha`;
- `RELATORIO_PERIODICO`, o lado IoB, que só leva acionamentos, ciclos e disponibilidade, sem dado pessoal.

**Objetivo:** escolher o destino dos eventos e o formato do vídeo.

**Resultado:** a equipe escolheu um endpoint mínimo do AJA e um vídeo misto, com paleta neutra do AJA e a fonte IBM Plex Sans, em tom sóbrio. O projeto do Wokwi ficou público.

**Objetivo:** listar as tecnologias usadas e aprovar antes de criar recursos nas contas da equipe.

**Resultado:** a pilha ficou assim: Wokwi, ESP32 com Arduino core 3.x e FreeRTOS, ArduinoJson, LiquidCrystal I2C, FastAPI, SQLite, Docker e Railway. A equipe aprovou a publicação da API no Railway antes de ela ser feita.

## 3. Construir o circuito e o firmware

**Objetivo:** montar no Wokwi o circuito e o código do projeto "SEM2 - GS - PHYSICAL COMPUTING IOT & IOB", com link para compartilhar.

**Resultado:** o firmware fica em `firmware/sketch.ino`, `firmware/diagram.json` e `firmware/libraries.txt`. O circuito tem:
- ESP32 DevKit C v4;
- botoeira lida por interrupção;
- LEDs Pare e Atravesse;
- buzzer para o som de localização e da travessia;
- LCD 1602 I2C para a contagem;
- LED âmbar de manutenção;
- chaves que simulam falhas e queda de rede.

A rede roda numa tarefa FreeRTOS separada, com fila de 24 eventos. Cada falha é verificada por pinos de retorno elétrico e só é confirmada após 3 leituras seguidas.

**Objetivo:** deixar o circuito organizado.

**Resultado:** o layout foi refeito por blocos (manutenção e rede, semáforo de pedestre, sinal sonoro, botoeira, display), com fios em ângulo reto, símbolos de GND e VCC e títulos por bloco.

**Objetivo:** deixar os comentários do código gerais, essenciais e conclusivos.

**Resultado:** o firmware tem um cabeçalho geral e um comentário por bloco que diz o que o bloco faz e por quê, no lugar de comentários linha a linha.

## 4. Construir a API e a página do tópico

**Objetivo:** criar o endpoint mínimo do AJA que recebe os eventos do circuito.

**Resultado:** `api/main.py`, em FastAPI e SQLite, tem:
- `POST /api/v1/dispositivos/eventos`, autenticado pela chave do dispositivo, com limite de 4 KB e eventos repetidos ignorados;
- `GET /api/v1/topicos/{id}`;
- reinício protegido da demonstração.

A página `api/static/topico.html` mostra a situação do equipamento, os indicadores de impacto, a linha do tempo de evidências e o dossiê. A API está publicada no Railway com `Dockerfile` e volume.

**Objetivo:** entender por que o Wokwi envia por HTTP sem TLS.

**Resultado:** com HTTPS, a simulação caía para cerca de 3% da velocidade. Por isso o Wokwi envia por HTTP a um proxy TCP da mesma API. O README registra que, em campo, o envio seria por HTTPS.

## 5. Revisar

**Objetivo:** confirmar que a simulação funciona por completo, integrada à API publicada.

**Resultado:** os testes no Wokwi tiveram respostas HTTP 201 e cobriram:
- falha e conserto do som, da luz verde e da botoeira travada;
- queda de rede com fila;
- travessia completa;
- relatório periódico.

**Objetivo:** corrigir a leitura do desenho do circuito. Os triângulos pareciam polo positivo, o texto preto ficava ilegível no tema escuro e os rótulos ficavam embaixo dos LEDs.

**Resultado:** o desenho ganhou a legenda "Triângulo = GND (terra, 0 V) | Seta VCC = alimentação", e os rótulos Pare, Atravesse e Manutenção foram para o lado dos LEDs. A imagem do README usa o tema claro, porque o texto do Wokwi é sempre preto.

**Objetivo:** entender como a acessibilidade aparece no painel.

**Resultado:** o painel tem:
- avisos anunciados por leitor de tela (`aria-live`);
- VLibras;
- contraste mínimo de 4,5:1;
- respeito à opção de reduzir movimento;
- textos inseridos sem HTML.

Esses pontos estão numa tabela do README.

## 6. Documentar

**Objetivo:** escrever o README com destaque para a premissa do AJA e a ligação entre o hardware e a plataforma.

**Resultado:** o `README.md` reúne:
- equipe no topo e tabela de entregáveis;
- premissa e problema;
- diagrama hardware↔AJA com a tabela de eventos;
- IoB e o circuito, com tabela de pinos e explicação do FreeRTOS;
- API, como executar, segurança e acessibilidade;
- limitações, ODS e referências com links.

## 7. Produzir o vídeo

**Objetivo:** montar o roteiro do vídeo e da narração.

**Resultado:** o roteiro tem 10 blocos:
- abertura, o AJA e o problema, em cenas animadas;
- quatro tomadas gravadas no Wokwi e no painel: circuito e travessia normal, falha que vira evidência e conserto, queda de internet, IoB;
- encerramento animado.
