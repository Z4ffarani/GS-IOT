# Prompts usados com IA

Registro dos pedidos feitos ao assistente de IA (Claude, da Anthropic) durante o desenvolvimento desta entrega, como pede o briefing da Global Solution 2026.2 (CitizenTech), na disciplina de Physical Computing (IoT & IoB). Os pedidos estão resumidos e reescritos por etapa, na ordem do trabalho, com o que resultou de cada um. As decisões de escopo, revisão e aprovação foram da equipe.

---

## 1. Entender a tarefa

**Pedido:** ler o enunciado de Physical Computing e o material do AJA, sem começar a implementar, e explicar se uma API ligada ao circuito ficaria conectada à plataforma, já que o circuito não pode ser um projeto paralelo.

**Resultado:** levantamento dos requisitos: projeto ESP32 no Wokwi com link, vídeo de até 4 minutos e arquivo com os integrantes. Ficou claro que o circuito precisa alimentar o AJA. A equipe decidiu avançar por partes.

**Pedido:** explicar o que se pensou em desenvolver.

**Resultado:** proposta do AJA Sentinela, um ESP32 dentro do semáforo de pedestre que opera a travessia acessível, verifica se o som, a luz verde e a botoeira funcionam e envia as falhas ao tópico da travessia no AJA.

## 2. Definir o escopo e a ligação com o AJA

**Pedido:** enfatizar a ligação entre o hardware e o AJA e a premissa da plataforma, partindo da ideia da equipe de reportar mau funcionamento e contar quantas pessoas precisaram do equipamento durante a falha.

**Resultado:** quatro eventos definidos: `DISPOSITIVO_INICIADO`, `FALHA_DETECTADA`, `FALHA_RESOLVIDA` (com `tempo_inoperante_s` e `acionamentos_durante_falha`) e `RELATORIO_PERIODICO`. Este último é o lado IoB e não usa dado pessoal: só acionamentos, ciclos e disponibilidade.

**Pedido:** escolher o destino dos eventos e o formato do vídeo.

**Resultado:** a equipe escolheu um endpoint mínimo do AJA e um vídeo misto, com paleta neutra do AJA e a fonte IBM Plex Sans, em tom sóbrio. O projeto do Wokwi ficou público e o código foi para o repositório `Z4ffarani/GS-IoT`.

**Pedido:** informar cada tecnologia usada e pedir aprovação antes de criar algo nas contas da equipe.

**Resultado:** tecnologias listadas: Wokwi, ESP32 com Arduino core 3.x e FreeRTOS, ArduinoJson, LiquidCrystal I2C, FastAPI, SQLite, Docker e Railway. A criação do serviço no Railway foi aprovada pela equipe antes de ser feita.

## 3. Construir o circuito e o firmware

**Pedido:** montar no Wokwi o circuito e o código do projeto "SEM2 - GS - PHYSICAL COMPUTING IOT & IOB", com link para compartilhar.

**Resultado:** `firmware/sketch.ino`, `firmware/diagram.json` e `firmware/libraries.txt` no projeto, com estes componentes:
- ESP32 DevKit C v4;
- botoeira lida por interrupção;
- LEDs Pare e Atravesse;
- buzzer para o som de localização e da travessia;
- LCD 1602 I2C para a contagem;
- LED âmbar de manutenção;
- chaves que simulam falhas e queda de rede.

A rede roda numa tarefa FreeRTOS separada, com fila de 24 eventos. Cada falha é verificada por pinos de retorno elétrico e só é confirmada após 3 leituras seguidas.

**Pedido:** deixar o circuito organizado.

**Resultado:** layout refeito por blocos (manutenção e rede, semáforo de pedestre, sinal sonoro, botoeira, display), com fios roteados, símbolos de GND e VCC e rótulos por bloco.

**Pedido:** usar menos comentários no código, gerais mas essenciais e conclusivos.

**Resultado:** firmware com um cabeçalho geral e um comentário conclusivo por bloco, no lugar de comentários linha a linha.

## 4. Construir a API e a página do tópico

**Pedido:** criar o endpoint mínimo do AJA que recebe os eventos do circuito.

**Resultado:** `api/main.py` em FastAPI e SQLite, com:
- `POST /api/v1/dispositivos/eventos`: autenticado por chave do dispositivo, limite de 4 KB e eventos repetidos ignorados;
- `GET /api/v1/topicos/{id}`;
- reinício protegido da demonstração.

A página `api/static/topico.html` mostra a situação do equipamento, os indicadores de impacto, a linha do tempo de evidências e o dossiê. A API está publicada no Railway com `Dockerfile` e volume.

**Pedido:** explicar a escolha de HTTP sem TLS no Wokwi.

**Resultado:** com HTTPS, a simulação caía para cerca de 3% da velocidade. A solução foi um proxy TCP com HTTP no Wokwi, e o README registra que, em campo, o envio usa HTTPS.

## 5. Revisar

**Pedido:** confirmar que o simulador funciona por completo.

**Resultado:** testes no Wokwi integrados à API publicada (respostas HTTP 201) cobriram:
- falha e conserto do som, da luz verde e da botoeira travada;
- queda de rede com fila;
- travessia completa;
- relatório periódico.

**Pedido:** corrigir a leitura do desenho do circuito. Os triângulos pareciam polo positivo, o texto preto ficava ilegível no tema escuro e os rótulos estavam embaixo dos LEDs.

**Resultado:** a legenda no desenho diz "Triângulo = GND (terra, 0 V) | Seta VCC = alimentação", e os rótulos Pare, Atravesse e Manutenção foram para o lado dos LEDs. A imagem do README passou a usar o tema claro, porque o texto do Wokwi é sempre preto.

**Pedido:** explicar como a acessibilidade aparece no painel.

**Resultado:** o painel tem:
- avisos anunciados por leitor de tela (`aria-live`);
- VLibras;
- contraste mínimo de 4,5:1;
- respeito à opção de reduzir movimento;
- textos inseridos sem HTML.

Esses pontos estão em uma tabela no README.

## 6. Documentar

**Pedido:** escrever o README destacando a premissa do AJA e a ligação entre o hardware e a plataforma.

**Resultado:** o `README.md` reúne:
- equipe no topo e tabela de entregáveis;
- premissa e problema;
- diagrama hardware↔AJA com a tabela de eventos;
- IoB, circuito com tabela de pinos e explicação do FreeRTOS;
- API, como executar, segurança e acessibilidade;
- limitações, ODS e referências.

**Pedido:** colocar links nas referências e explicar o termo FreeRTOS.

**Resultado:** referências com links (NBR 9050, Manual Brasileiro de Sinalização de Trânsito, ODS, WCAG, VLibras, FreeRTOS, Espressif, Wokwi e FastAPI) e uma seção curta sobre o que é o FreeRTOS e por que a rede roda numa tarefa separada.

**Pedido:** gerar o arquivo com os integrantes.

**Resultado:** `integrantes.txt` com os três integrantes e RMs, o link do Wokwi e o link do repositório.

## 7. Preparar o vídeo

**Pedido:** passar o roteiro do vídeo e da narração em mensagem.

**Resultado:** roteiro em 10 blocos: abertura, o AJA e o problema em cenas animadas, e as tomadas A a D gravadas no Wokwi e no painel (circuito e travessia normal, falha que vira evidência e conserto, queda de internet, IoB), terminando no encerramento animado.

**Pedido:** explicar o que gravar na tela, por que existe um link específico para reiniciar a demonstração e para que servem vários toques na botoeira em poucos segundos.

**Resultado:** cada tomada foi descrita passo a passo. O link de reinício leva uma chave, para que só a equipe consiga zerar o tópico antes de gravar. Os toques repetidos durante a falha mostram a contagem de acionamentos subindo no painel.

**Pedido:** editar o vídeo com as gravações e a narração da equipe, mostrando só o circuito e a interface, sem a barra do navegador e a barra de tarefas.

**Resultado:** as gravações em HDR foram convertidas para cor padrão e recortadas na área do circuito e do painel. As cenas animadas foram montadas na paleta neutra do AJA, e a narração da equipe foi sincronizada com as tomadas.

## 8. Revisar e entregar

**Pedido:** registrar os pedidos feitos à IA num `prompts.md`.

**Resultado:** este arquivo, organizado por etapa do trabalho.
