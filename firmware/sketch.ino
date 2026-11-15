/*
  AJA Sentinela - travessia acessivel monitorada
  CitizenTech AJA | GS FIAP 2026.2 | Physical Computing: IoT & IoB

  O ESP32 opera um semaforo de pedestres acessivel (botoeira, luzes, sinal
  sonoro e contagem regressiva no display) e confere sozinho se o som, a luz
  verde e a botoeira funcionam. Quando uma falha se confirma, avisa a API do
  AJA, que anexa o aviso como evidencia ao topico da travessia; quando o
  componente volta, avisa de novo com o tempo parado e quantas pessoas
  acionaram a botoeira durante a falha. Relatorios periodicos mostram o uso
  da travessia e se o conserto se mantem. Nenhum dado pessoal e coletado.

  Chaves de simulacao no Wokwi: alavanca a direita = normal,
  a esquerda = simula a falha.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------------- Configuracao ----------------
// Tempos encurtados para a demonstracao no Wokwi. O envio usa HTTP por um
// proxy TCP do Railway porque o TLS em software deixa o simulador lento demais;
// em campo, o mesmo codigo envia por HTTPS com o certificado da API fixado.
static const char *WIFI_SSID = "Wokwi-GUEST";
static const char *WIFI_SENHA = "";
static const int WIFI_CANAL = 6;

static const char *AJA_API_URL = "http://junction.proxy.rlwy.net:30564/api/v1/dispositivos/eventos";
static const char *AJA_DEVICE_ID = "AJA-PDOR-TRAV-001";
static const char *AJA_DEVICE_KEY = "aja-_JPURKO1MjEz6COTsdVKLIbq";  // chave de demonstracao
static const char *LOCAL_REFERENCIA = "Travessia monitorada 01 - Parque Doroteia, Sao Paulo/SP";
static const char *FIRMWARE_VERSAO = "1.0.0";

const uint32_t T_VERMELHO_MIN_MS = 4000;
const uint32_t T_ESPERA_MS = 3000;
const uint32_t T_VERDE_MS = 8000;
const uint32_t T_PISCANDO_MS = 5000;
const uint32_t T_BOTAO_TRAVADO_MS = 8000;
const uint32_t T_RELATORIO_MS = 60000;
const uint32_t T_VERIFICACAO_MS = 1000;
const uint32_t T_ANTIRREPIQUE_MS = 250;
const uint8_t VERIFICACOES_PARA_CONFIRMAR = 3;

const uint8_t PIN_BOTOEIRA = 4;
const uint8_t PIN_LUZ_VERMELHA = 19;
const uint8_t PIN_LUZ_VERDE = 18;
const uint8_t PIN_RETORNO_LUZ = 5;
const uint8_t PIN_SOM = 17;
const uint8_t PIN_RETORNO_SOM = 16;
const uint8_t PIN_ALERTA = 33;
const uint8_t PIN_SIMULA_SEM_REDE = 32;
const uint8_t PIN_I2C_SDA = 23;
const uint8_t PIN_I2C_SCL = 22;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- Componentes monitorados ----------------
// Cada componente acumula verificacoes; a falha (e o conserto) so e
// confirmada depois de 3 seguidas, para evitar alarme falso.
static const char *PUB_VISUAL[] = {"deficiencia_visual"};
static const char *PUB_AUDITIVA[] = {"deficiencia_auditiva"};
static const char *PUB_AMBOS[] = {"deficiencia_visual", "deficiencia_auditiva"};

struct Monitor {
  const char *componente;
  const char *rotulo;
  const char **publico;
  uint8_t nPublico;
  const char *descricaoFalha;
  uint8_t falhasSeguidas;
  uint8_t okSeguidos;
  bool emFalha;
  uint32_t inicioFalhaMs;
  uint32_t acionamentosDuranteFalha;
  uint32_t msEmFalhaNaJanela;
};

Monitor monSom = {"SINAL_SONORO", "SOM", PUB_VISUAL, 1,
                  "Sinal sonoro sem resposta: pessoas cegas ou com baixa visao "
                  "nao sabem quando e seguro atravessar",
                  0, 0, false, 0, 0, 0};
Monitor monLuz = {"LAMPADA_VERDE_PEDESTRE", "LUZ VERDE", PUB_AUDITIVA, 1,
                  "Lampada verde sem resposta: pessoas surdas dependem do sinal "
                  "visual para atravessar",
                  0, 0, false, 0, 0, 0};
Monitor monBotao = {"BOTOEIRA", "BOTOEIRA", PUB_AMBOS, 2,
                    "Botoeira travada: novos pedidos de travessia nao sao "
                    "registrados",
                    0, 0, false, 0, 0, 0};
Monitor *monitores[] = {&monSom, &monLuz, &monBotao};
const uint8_t N_MONITORES = sizeof(monitores) / sizeof(monitores[0]);

// ---------------- Estado ----------------
struct Evento {
  char json[640];
};
QueueHandle_t filaEventos;
const uint8_t TAMANHO_FILA = 24;
volatile bool redeOnline = false;
volatile uint32_t eventosDescartados = 0;
uint32_t seqEvento = 0;

enum Fase { PARE, ESPERA, VERDE, PISCANDO };
Fase fase = PARE;
uint32_t inicioFaseMs = 0;
bool pedidoPendente = false;
bool luzVerdeLigada = false;
uint32_t proximoBipMs = 0;

struct {
  bool ativo;
  uint32_t fimMs;
} bip = {false, 0};

volatile bool bordaBotoeira = false;
bool botaoPressionado = false;
uint32_t pressionadoDesdeMs = 0;
uint32_t ultimoAcionamentoMs = 0;

uint32_t inicioJanelaMs = 0;
uint32_t acionamentosJanela = 0;
uint32_t ciclosJanela = 0;

uint32_t ultimaVerificacaoMs = 0;
uint32_t ultimoDisplayMs = 0;
char linhaLcd[2][17] = {"", ""};

// ---------------- Eventos para o AJA ----------------
// Os eventos sao montados aqui e entram numa fila; a tarefa de rede os
// envia depois. Relatorios podem ser descartados com a fila quase cheia,
// avisos de falha nao.
bool horaAtualIso(char *buf, size_t len) {
  time_t agora = time(nullptr);
  if (agora < 1700000000) return false;
  struct tm t;
  localtime_r(&agora, &t);
  strftime(buf, len, "%Y-%m-%dT%H:%M:%S-03:00", &t);
  return true;
}

uint32_t inicioNaJanela(uint32_t inicioMs) {
  return (int32_t)(inicioMs - inicioJanelaMs) > 0 ? inicioMs : inicioJanelaMs;
}

void preencherCabecalho(JsonDocument &doc, const char *tipo) {
  doc["dispositivo_id"] = AJA_DEVICE_ID;
  doc["equipamento"] = "SEMAFORO_SONORO_COM_BOTOEIRA";
  doc["local_ref"] = LOCAL_REFERENCIA;
  doc["tipo"] = tipo;
  doc["seq"] = ++seqEvento;
  doc["uptime_s"] = millis() / 1000;
  char ts[32];
  if (horaAtualIso(ts, sizeof(ts))) {
    doc["ts"] = ts;
  } else {
    doc["ts"] = nullptr;
  }
}

void enfileirar(JsonDocument &doc, bool descartavel) {
  Evento ev;
  size_t n = serializeJson(doc, ev.json, sizeof(ev.json));
  if (n == 0 || n >= sizeof(ev.json) - 1) {
    Serial.println("[AJA] ERRO: evento grande demais, nao enfileirado");
    return;
  }
  Serial.printf("[AJA] Evento %s\n", ev.json);

  if (descartavel && uxQueueSpacesAvailable(filaEventos) < 8) {
    eventosDescartados = eventosDescartados + 1;
    Serial.println("[AJA] Fila quase cheia: relatorio descartado");
    return;
  }
  if (xQueueSend(filaEventos, &ev, 0) != pdTRUE) {
    eventosDescartados = eventosDescartados + 1;
    Serial.println("[AJA] Fila cheia: evento descartado");
  }
}

void adicionarPublico(JsonDocument &doc, const Monitor &m) {
  JsonArray pub = doc["publico_afetado"].to<JsonArray>();
  for (uint8_t i = 0; i < m.nPublico; i++) pub.add(m.publico[i]);
}

void abrirFalha(Monitor &m) {
  m.emFalha = true;
  m.inicioFalhaMs = millis();
  m.acionamentosDuranteFalha = 0;

  JsonDocument doc;
  preencherCabecalho(doc, "FALHA_DETECTADA");
  doc["componente"] = m.componente;
  doc["severidade"] = "ALTA";
  adicionarPublico(doc, m);
  doc["descricao"] = m.descricaoFalha;
  doc["verificacoes_com_falha"] = m.falhasSeguidas;
  Serial.printf("[AJA] >>> FALHA DETECTADA: %s\n", m.componente);
  enfileirar(doc, false);
}

void encerrarFalha(Monitor &m) {
  uint32_t agora = millis();
  m.msEmFalhaNaJanela += agora - inicioNaJanela(m.inicioFalhaMs);
  m.emFalha = false;

  JsonDocument doc;
  preencherCabecalho(doc, "FALHA_RESOLVIDA");
  doc["componente"] = m.componente;
  adicionarPublico(doc, m);
  doc["tempo_inoperante_s"] = (agora - m.inicioFalhaMs) / 1000;
  doc["acionamentos_durante_falha"] = m.acionamentosDuranteFalha;
  doc["descricao"] = "Componente voltou a responder em verificacoes seguidas";
  Serial.printf("[AJA] <<< FALHA RESOLVIDA: %s\n", m.componente);
  enfileirar(doc, false);
}

void registrarVerificacao(Monitor &m, bool ok) {
  if (ok) {
    m.falhasSeguidas = 0;
    if (m.okSeguidos < 255) m.okSeguidos++;
    if (m.emFalha && m.okSeguidos >= VERIFICACOES_PARA_CONFIRMAR) encerrarFalha(m);
  } else {
    m.okSeguidos = 0;
    if (m.falhasSeguidas < 255) m.falhasSeguidas++;
    if (!m.emFalha && m.falhasSeguidas >= VERIFICACOES_PARA_CONFIRMAR) abrirFalha(m);
  }
}

bool algumaFalhaAtiva() {
  for (uint8_t i = 0; i < N_MONITORES; i++)
    if (monitores[i]->emFalha) return true;
  return false;
}

// Relatorio de uso (IoB): so contagens agregadas e a disponibilidade de cada
// componente na janela; e o que mostra, depois do conserto, que ele se manteve.
void enviarRelatorio() {
  uint32_t agora = millis();
  uint32_t janela = agora - inicioJanelaMs;

  JsonDocument doc;
  preencherCabecalho(doc, "RELATORIO_PERIODICO");
  doc["janela_s"] = janela / 1000;
  doc["acionamentos_botoeira"] = acionamentosJanela;
  doc["ciclos_travessia"] = ciclosJanela;

  JsonObject disp = doc["disponibilidade_pct"].to<JsonObject>();
  JsonArray ativas = doc["falhas_ativas"].to<JsonArray>();
  for (uint8_t i = 0; i < N_MONITORES; i++) {
    Monitor &m = *monitores[i];
    uint32_t msFalha = m.msEmFalhaNaJanela;
    if (m.emFalha) msFalha += agora - inicioNaJanela(m.inicioFalhaMs);
    float pct = janela ? 100.0f * (float)(janela - msFalha) / (float)janela : 100.0f;
    disp[m.componente] = roundf(pct * 10.0f) / 10.0f;
    if (m.emFalha) ativas.add(m.componente);
    m.msEmFalhaNaJanela = 0;
  }
  doc["eventos_na_fila"] = uxQueueMessagesWaiting(filaEventos);
  doc["eventos_descartados"] = eventosDescartados;
  doc["rede_online"] = (bool)redeOnline;
  if (redeOnline) doc["wifi_rssi_dbm"] = WiFi.RSSI();

  acionamentosJanela = 0;
  ciclosJanela = 0;
  inicioJanelaMs = agora;
  enfileirar(doc, true);
}

// ---------------- Verificacao eletrica ----------------
// O pino de retorno fica depois do fio que vai ao componente. Com o circuito
// integro ele le o mesmo sinal enviado; com o fio rompido, o pull-down o
// leva a LOW e a verificacao falha. Com a luz verde apagada, um pulso de
// 200 us (invisivel) faz o teste.
void configurarSom() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_SOM, 2000, 8);
#else
  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_SOM, 0);
#endif
}

void somTocar(uint32_t freq) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(PIN_SOM, freq);
#else
  ledcWriteTone(0, freq);
#endif
}

void somParar() { somTocar(0); }

bool retornoPresente(uint8_t pino, uint16_t amostras, uint16_t intervaloUs) {
  for (uint16_t i = 0; i < amostras; i++) {
    if (digitalRead(pino) == HIGH) return true;
    delayMicroseconds(intervaloUs);
  }
  return false;
}

void iniciarBip(uint32_t freq, uint32_t duracaoMs) {
  somTocar(freq);
  bip.ativo = true;
  bip.fimMs = millis() + duracaoMs;
  delayMicroseconds(300);
  registrarVerificacao(monSom, retornoPresente(PIN_RETORNO_SOM, 150, 15));
}

void atualizarBip(uint32_t agora) {
  if (bip.ativo && (int32_t)(agora - bip.fimMs) >= 0) {
    somParar();
    bip.ativo = false;
  }
}

void ligarVerde(bool ligar) {
  luzVerdeLigada = ligar;
  digitalWrite(PIN_LUZ_VERDE, ligar ? HIGH : LOW);
}

bool testarLampadaVerde() {
  if (luzVerdeLigada) return digitalRead(PIN_RETORNO_LUZ) == HIGH;
  digitalWrite(PIN_LUZ_VERDE, HIGH);
  delayMicroseconds(200);
  bool ok = digitalRead(PIN_RETORNO_LUZ) == HIGH;
  digitalWrite(PIN_LUZ_VERDE, LOW);
  return ok;
}

void verificacoesPeriodicas(uint32_t agora) {
  if (agora - ultimaVerificacaoMs < T_VERIFICACAO_MS) return;
  ultimaVerificacaoMs = agora;
  registrarVerificacao(monLuz, testarLampadaVerde());
  bool travado = botaoPressionado && (agora - pressionadoDesdeMs) > T_BOTAO_TRAVADO_MS;
  registrarVerificacao(monBotao, !travado);
}

// ---------------- Travessia ----------------
// Padrao brasileiro: verde = atravesse, vermelho piscante = finalize,
// vermelho = pare. Bip lento de localizacao no vermelho (para a pessoa cega
// achar a botoeira) e bips mais rapidos durante a travessia.
void mudarFase(Fase nova, uint32_t agora) {
  if (fase == PISCANDO && nova == PARE) ciclosJanela++;
  fase = nova;
  inicioFaseMs = agora;
  switch (nova) {
    case PARE:
      pedidoPendente = false;
      ligarVerde(false);
      digitalWrite(PIN_LUZ_VERMELHA, HIGH);
      break;
    case ESPERA:
      break;
    case VERDE:
      digitalWrite(PIN_LUZ_VERMELHA, LOW);
      ligarVerde(true);
      proximoBipMs = agora;
      Serial.println("[TRAVESSIA] Verde: pode atravessar");
      break;
    case PISCANDO:
      ligarVerde(false);
      proximoBipMs = agora;
      Serial.println("[TRAVESSIA] Vermelho piscante: finalize a travessia");
      break;
  }
}

void aoPressionarBotoeira(uint32_t agora) {
  acionamentosJanela++;
  for (uint8_t i = 0; i < N_MONITORES; i++)
    if (monitores[i]->emFalha) monitores[i]->acionamentosDuranteFalha++;

  if (fase == PARE && !pedidoPendente) {
    pedidoPendente = true;
    Serial.println("[TRAVESSIA] Pedido de travessia registrado");
    if (!bip.ativo) iniciarBip(1200, 150);
  }
}

// A botoeira e lida por interrupcao para nenhum toque se perder; o nivel do
// pino mostra se ela ficou travada.
void IRAM_ATTR aoTocarBotoeira() { bordaBotoeira = true; }

void lerBotoeira(uint32_t agora) {
  bool nivel = digitalRead(PIN_BOTOEIRA) == LOW;
  if (nivel && !botaoPressionado) pressionadoDesdeMs = agora;
  botaoPressionado = nivel;

  if (bordaBotoeira) {
    bordaBotoeira = false;
    if (agora - ultimoAcionamentoMs >= T_ANTIRREPIQUE_MS) {
      ultimoAcionamentoMs = agora;
      aoPressionarBotoeira(agora);
    }
  }
}

void atualizarTravessia(uint32_t agora) {
  uint32_t naFase = agora - inicioFaseMs;
  switch (fase) {
    case PARE:
      if (pedidoPendente && naFase >= T_VERMELHO_MIN_MS) mudarFase(ESPERA, agora);
      break;
    case ESPERA:
      if (naFase >= T_ESPERA_MS) mudarFase(VERDE, agora);
      break;
    case VERDE:
      if (naFase >= T_VERDE_MS) mudarFase(PISCANDO, agora);
      break;
    case PISCANDO:
      digitalWrite(PIN_LUZ_VERMELHA, ((naFase / 300) % 2) == 0 ? HIGH : LOW);
      if (naFase >= T_PISCANDO_MS) mudarFase(PARE, agora);
      break;
  }

  if (!bip.ativo && (int32_t)(agora - proximoBipMs) >= 0) {
    uint32_t freq, dur, periodo;
    if (fase == VERDE) {
      freq = 1500; dur = 100; periodo = 400;
    } else if (fase == PISCANDO) {
      freq = 1500; dur = 60; periodo = 200;
    } else {
      freq = 880; dur = 40; periodo = 1000;
    }
    iniciarBip(freq, dur);
    proximoBipMs = agora + periodo;
  }
}

void atualizarAlerta(uint32_t agora) {
  bool ligado = algumaFalhaAtiva() && ((agora / 500) % 2 == 0);
  digitalWrite(PIN_ALERTA, ligado ? HIGH : LOW);
}

// ---------------- Display ----------------
// Linha 1: fase e contagem regressiva (essencial para a pessoa surda).
// Linha 2: falhas ativas primeiro; sem falhas, estado do equipamento e da rede.
void escreverLinha(uint8_t linha, const char *texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  if (strcmp(buf, linhaLcd[linha]) != 0) {
    lcd.setCursor(0, linha);
    lcd.print(buf);
    strcpy(linhaLcd[linha], buf);
  }
}

void atualizarDisplay(uint32_t agora) {
  if (agora - ultimoDisplayMs < 200) return;
  ultimoDisplayMs = agora;

  char l1[17];
  uint32_t naFase = agora - inicioFaseMs;
  switch (fase) {
    case PARE:
      snprintf(l1, sizeof(l1), "%s", pedidoPendente ? "Pedido recebido" : "PARE - use botao");
      break;
    case ESPERA:
      snprintf(l1, sizeof(l1), "Aguarde o verde");
      break;
    case VERDE: {
      uint8_t resta = min<uint32_t>((T_VERDE_MS + T_PISCANDO_MS - naFase + 999) / 1000, 99);
      snprintf(l1, sizeof(l1), "ATRAVESSE   %2us", (unsigned)resta);
      break;
    }
    case PISCANDO: {
      uint8_t resta = min<uint32_t>((T_PISCANDO_MS - naFase + 999) / 1000, 99);
      snprintf(l1, sizeof(l1), "FINALIZE    %2us", (unsigned)resta);
      break;
    }
  }
  escreverLinha(0, l1);

  char l2[17];
  Monitor *emFalha[N_MONITORES];
  uint8_t nFalhas = 0;
  for (uint8_t i = 0; i < N_MONITORES; i++)
    if (monitores[i]->emFalha) emFalha[nFalhas++] = monitores[i];

  uint8_t paginas = nFalhas + 1;
  uint8_t pagina = (agora / 2500) % paginas;
  if (pagina < nFalhas) {
    snprintf(l2, sizeof(l2), "FALHA:%s", emFalha[pagina]->rotulo);
  } else if (nFalhas == 0 && (agora / 2500) % 2 == 0) {
    snprintf(l2, sizeof(l2), "Equipamento OK");
  } else {
    snprintf(l2, sizeof(l2), "Rede:%s Fila:%u", redeOnline ? "ON" : "OFF",
             (unsigned)uxQueueMessagesWaiting(filaEventos));
  }
  escreverLinha(1, l2);
}

// ---------------- Rede ----------------
// Roda numa tarefa separada (nucleo 0): o semaforo nunca espera a internet.
// Um evento so sai da fila depois que a API confirma o recebimento (HTTP 2xx);
// sem rede, eles esperam e sao enviados em ordem quando a conexao volta.
bool enviarEvento(const Evento &ev) {
  if (strlen(AJA_API_URL) == 0) {
    Serial.println("[REDE] API nao configurada: evento entregue apenas ao Serial");
    return true;
  }
  HTTPClient http;
  WiFiClientSecure clienteSeguro;
  WiFiClient cliente;
  bool iniciou;
  if (strncmp(AJA_API_URL, "https", 5) == 0) {
    clienteSeguro.setInsecure();  // demonstracao; em producao, fixar o certificado
    iniciou = http.begin(clienteSeguro, AJA_API_URL);
  } else {
    iniciou = http.begin(cliente, AJA_API_URL);
  }
  if (!iniciou) return false;

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-AJA-Device-Id", AJA_DEVICE_ID);
  http.addHeader("X-AJA-Device-Key", AJA_DEVICE_KEY);
  int codigo = http.POST((uint8_t *)ev.json, strlen(ev.json));
  http.end();
  Serial.printf("[REDE] POST %s -> HTTP %d\n", AJA_API_URL, codigo);
  return codigo >= 200 && codigo < 300;
}

void tarefaRede(void *) {
  WiFi.mode(WIFI_STA);
  bool ntpConfigurado = false;
  bool primeiraTentativa = true;
  uint32_t ultimaTentativaMs = 0;
  uint32_t esperaMs = 1000;

  for (;;) {
    if (digitalRead(PIN_SIMULA_SEM_REDE) == LOW) {
      if (redeOnline || WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect();
        Serial.println("[REDE] Queda de internet simulada: eventos ficam na fila");
        primeiraTentativa = true;
      }
      redeOnline = false;
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }

    if (WiFi.status() != WL_CONNECTED) {
      redeOnline = false;
      if (primeiraTentativa || millis() - ultimaTentativaMs > 10000) {
        Serial.println("[REDE] Conectando ao Wi-Fi...");
        WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
        ultimaTentativaMs = millis();
        primeiraTentativa = false;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    if (!redeOnline) {
      redeOnline = true;
      Serial.printf("[REDE] Conectado (IP %s). Eventos na fila: %u\n",
                    WiFi.localIP().toString().c_str(),
                    (unsigned)uxQueueMessagesWaiting(filaEventos));
      if (!ntpConfigurado) {
        configTzTime("<-03>3", "pool.ntp.org", "time.google.com");
        ntpConfigurado = true;
      }
    }

    Evento ev;
    if (xQueuePeek(filaEventos, &ev, pdMS_TO_TICKS(300)) == pdTRUE) {
      if (enviarEvento(ev)) {
        xQueueReceive(filaEventos, &ev, 0);
        esperaMs = 1000;
      } else {
        Serial.printf("[REDE] Falha no envio, nova tentativa em %lu ms\n",
                      (unsigned long)esperaMs);
        vTaskDelay(pdMS_TO_TICKS(esperaMs));
        esperaMs = min<uint32_t>(esperaMs * 2, 30000);
      }
    }
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("==============================================");
  Serial.println(" AJA Sentinela - travessia acessivel monitorada");
  Serial.printf(" Dispositivo %s | firmware %s\n", AJA_DEVICE_ID, FIRMWARE_VERSAO);
  Serial.println("==============================================");

  pinMode(PIN_BOTOEIRA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTOEIRA), aoTocarBotoeira, FALLING);
  pinMode(PIN_SIMULA_SEM_REDE, INPUT_PULLUP);
  pinMode(PIN_RETORNO_LUZ, INPUT_PULLDOWN);
  pinMode(PIN_RETORNO_SOM, INPUT_PULLDOWN);
  pinMode(PIN_LUZ_VERMELHA, OUTPUT);
  pinMode(PIN_LUZ_VERDE, OUTPUT);
  pinMode(PIN_ALERTA, OUTPUT);
  configurarSom();
  somParar();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.init();
  lcd.backlight();
  escreverLinha(0, "AJA Sentinela");
  escreverLinha(1, "Parque Doroteia");
  delay(1500);

  filaEventos = xQueueCreate(TAMANHO_FILA, sizeof(Evento));
  xTaskCreatePinnedToCore(tarefaRede, "rede", 16384, nullptr, 1, nullptr, 0);

  uint32_t agora = millis();
  inicioJanelaMs = agora;
  mudarFase(PARE, agora);
  proximoBipMs = agora;

  JsonDocument doc;
  preencherCabecalho(doc, "DISPOSITIVO_INICIADO");
  doc["firmware"] = FIRMWARE_VERSAO;
  JsonArray comps = doc["componentes_monitorados"].to<JsonArray>();
  for (uint8_t i = 0; i < N_MONITORES; i++) comps.add(monitores[i]->componente);
  enfileirar(doc, false);
}

void loop() {
  uint32_t agora = millis();
  lerBotoeira(agora);
  atualizarTravessia(agora);
  atualizarBip(millis());
  verificacoesPeriodicas(agora);
  atualizarAlerta(agora);
  atualizarDisplay(agora);
  if (agora - inicioJanelaMs >= T_RELATORIO_MS) enviarRelatorio();
  delay(2);
}
