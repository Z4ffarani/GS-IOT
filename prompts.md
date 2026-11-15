# Registro de uso de IA — Physical Computing (IoT & IoB)

**Projeto:** AJA, GS FIAP 2026.2 CitizenTech
**Ferramenta:** Claude (Anthropic), modelo Claude Opus 5.5, em sessões de novembro de 2026.

A IA foi usada como copiloto. As decisões de produto (o recorte do AJA, o público e o bairro), a gravação e a narração do vídeo e a validação final no Wokwi são da equipe. Cada entrada abaixo resume o pedido, o que a IA produziu e o que a equipe conferiu ou mudou. Os prompts estão resumidos, com o sentido preservado.

---

### 1. Leitura do enunciado e proposta de circuito
- **Prompt:** ler o enunciado de Physical Computing e o material do AJA e explicar o que poderia ser desenvolvido, sem começar a implementar.
- **Objetivo:** entender os requisitos (ESP32 no Wokwi, vídeo de até 4 min e o circuito ligado ao AJA, não um projeto paralelo).
- **Resultado:** a proposta do *AJA Sentinela*, um semáforo de pedestre acessível que detecta as próprias falhas e envia eventos ao AJA.
- **Revisão da equipe:** pedimos para avançar por partes e confirmamos que o hardware precisava estar conectado à plataforma.

### 2. Ligação entre o hardware e o AJA
- **Prompt:** enfatizar a ligação entre o hardware e o AJA e a premissa da plataforma. Ideia da equipe: contar quantas pessoas precisaram do equipamento enquanto ele estava quebrado.
- **Resultado:** os eventos `FALHA_DETECTADA`, `FALHA_RESOLVIDA` (com tempo inoperante e acionamentos durante a falha) e `RELATORIO_PERIODICO` (IoB sem dado pessoal).
- **Revisão da equipe:** aprovamos a métrica de "acionamentos durante a falha" como evidência para o dossiê.

### 3. Escolha do destino dos eventos
- **Prompt:** perguntas sobre o endpoint, o estilo do vídeo, a paleta e a fonte.
- **Resultado:** uma API mínima do AJA (FastAPI + SQLite) publicada no Railway, com a página do tópico da travessia.
- **Revisão da equipe:** escolhemos o "endpoint mínimo do AJA", aprovamos a criação do projeto no Railway e pedimos para ser informados de cada tecnologia usada.

### 4. Montagem do circuito e do firmware no Wokwi
- **Prompt:** montar o circuito e o código na conta Wokwi da equipe, com nome definido e link compartilhável.
- **Resultado:** `firmware/sketch.ino`, `firmware/diagram.json` e `firmware/libraries.txt`. O firmware tem uma tarefa de rede separada (FreeRTOS), fila de eventos, verificação elétrica por pinos de retorno e falha confirmada após 3 leituras seguidas.
- **Revisão da equipe:** testamos os cenários no simulador e escolhemos salvar o projeto como público.

### 5. Organização visual do circuito
- **Prompt:** "Faça um circuito organizadinho".
- **Resultado:** o layout foi refeito por blocos (manutenção e rede, semáforo, sinal sonoro, botoeira, display), com fios roteados e rótulos.
- **Revisão da equipe:** apontamos rótulos ilegíveis e a dúvida sobre os triângulos (que são GND), o que levou à legenda no próprio desenho.

### 6. Comentários do código
- **Prompt:** usar menos comentários, gerais mas essenciais e que cheguem às conclusões.
- **Resultado:** um cabeçalho geral e um comentário conclusivo por bloco do firmware.
- **Revisão da equipe:** lemos o código bloco a bloco para conseguir explicar cada parte.

### 7. Página do tópico e acessibilidade
- **Prompt:** explicar como a acessibilidade aparece no painel.
- **Resultado:** a página `api/static` com avisos lidos por leitor de tela (`aria-live`), VLibras, contraste mínimo de 4,5:1 e respeito a "reduzir movimento".
- **Revisão da equipe:** conferimos a página no navegador durante a gravação.

### 8. Ajustes do README
- **Prompt:** adicionar a legenda dos símbolos, rótulos legíveis, explicação do FreeRTOS, rótulos ao lado dos LEDs e links nas referências.
- **Resultado:** README com a seção "Como ler o desenho", tabela de pinos, explicação do FreeRTOS, seção de segurança e referências com links.
- **Revisão da equipe:** decidimos compartilhar o repositório junto da entrega.

### 9. Roteiro do vídeo
- **Prompt:** passar o roteiro de vídeo e narração em mensagem.
- **Resultado:** o roteiro em tomadas (apresentação, circuito, falhas e painel do AJA).
- **Revisão da equipe:** a narração foi gravada por nós, com o texto adaptado, e as gravações de tela foram feitas por nós no Wokwi e no painel.

### 10. Edição do vídeo
- **Prompt:** cortar as gravações para mostrar só o circuito e a interface (sem a barra do navegador nem a barra de tarefas) e montar o vídeo com a narração e a trilha.
- **Resultado:** edição a partir das gravações da equipe, com conversão de cor das gravações HDR, cortes e sobreposições sóbrias.
- **Revisão da equipe:** a narração, as trilhas e a revisão do corte final são da equipe.

---

**Limites:** a chave do dispositivo no firmware é só de demonstração. Os relatos de moradores exibidos na página são simulados, e nenhum dado pessoal é coletado pelo dispositivo.
