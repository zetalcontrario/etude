#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include "config.h"

/*
  étude — ESP32 OSC Bridge
  Riceve 24 valori capacitivi da Arduino Mega via Serial2
  e li invia come messaggio OSC via WiFi UDP

  Formato in ingresso: $v1,v2,...,v24\n
  Formato in uscita:   /etude  f f f ... (24 float)

  ── Collegamento hardware ────────────────────────────────
  Arduino TX1 (pin 18) → 1kΩ → ESP32 RX2 (GPIO16)
  Partitore 5V→3.3V: 2kΩ dal nodo a GND
  GND comune

  ── Librerie ────────────────────────────────────────────
  WiFi  — inclusa nel core ESP32
  OSC   — Adrian Freed & Yotam Mann (Library Manager)

  ── Impostazioni board (Arduino IDE) ────────────────────
  Board:           ESP32S3 Dev Module
  USB CDC On Boot: Enabled
  Upload Mode:     USB-OTG (TinyUSB) oppure USB-CDC
  Flash Size:      16MB
  PSRAM:           OPI PSRAM

  ── Comandi debug (Serial Monitor USB) ──────────────────
  t — TEST:    self-test, genera e invia onda simulata via OSC
  i — INFO:    stato WiFi, IP, contatori, uptime
  p — PING:    pacchetto OSC con tutti i valori a 999
  z — ZERO:    pacchetto OSC con tutti i valori a 0
  v — VERBOSE: toggle stampa ogni pacchetto ricevuto
  l — LAST:    mostra l'ultimo pacchetto ricevuto
  w — WIFI:    forza riconnessione WiFi
  h — HELP:    mostra questa lista
*/

// ── Seriale da Arduino ───────────────────────────────────────
#define ARDUINO_SERIAL  Serial2
#define ARDUINO_RX_PIN  16
#define ARDUINO_TX_PIN  17
#define SERIAL_BAUD     115200

// ── Costanti ─────────────────────────────────────────────────
const int NUM_CHANNELS = 24;
const int BUFFER_SIZE  = 256;
const int LED_PIN      = 2;

// ── Stato ────────────────────────────────────────────────────
WiFiUDP udp;
char    rxBuffer[BUFFER_SIZE];
int     rxIndex      = 0;
bool    synced       = false;
bool    verboseMode  = false;
bool    selfTestMode = false;

float sensorValues[NUM_CHANNELS];

// ── Contatori diagnostici ────────────────────────────────────
unsigned long packetsReceived = 0;
unsigned long packetsSent     = 0;
unsigned long packetsDropped  = 0;
unsigned long bytesReceived   = 0;
unsigned long lastPacketTime  = 0;
unsigned long wifiReconnects  = 0;
unsigned long bootTime        = 0;


// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  ARDUINO_SERIAL.begin(SERIAL_BAUD, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);

  connectWiFi();
  udp.begin(OSC_PORT);

  bootTime = millis();

  Serial.println(F("═══════════════════════════════════════"));
  Serial.println(F("  étude — ESP32 OSC Bridge"));
  Serial.println(F("  Invia 'h' per i comandi debug"));
  Serial.println(F("═══════════════════════════════════════"));
}

// ════════════════════════════════════════════════════════════
void loop() {
  handleSerialCommands();

  if (selfTestMode) {
    generateAndSendTestData();
    delay(50);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // Lettura byte da Arduino e ricostruzione riga
  while (ARDUINO_SERIAL.available()) {
    char c = ARDUINO_SERIAL.read();
    bytesReceived++;

    if (c == '$') {
      rxIndex = 0;
      synced  = true;
      continue;
    }

    if (!synced) continue;

    if (c == '\n') {
      rxBuffer[rxIndex] = '\0';
      processLine();
      synced = false;
      continue;
    }

    if (rxIndex < BUFFER_SIZE - 1) {
      rxBuffer[rxIndex++] = c;
    } else {
      synced = false;
      packetsDropped++;
      if (verboseMode) Serial.println(F("[DROP] buffer overflow"));
    }
  }
}


// ════════════════════════════════════════════════════════════
// Parsing della riga e invio OSC
// ════════════════════════════════════════════════════════════
void processLine() {
  int   channel = 0;
  char* token   = strtok(rxBuffer, ",");

  while (token != NULL && channel < NUM_CHANNELS) {
    sensorValues[channel++] = (float)atol(token);
    token = strtok(NULL, ",");
  }

  if (channel != NUM_CHANNELS) {
    packetsDropped++;
    if (verboseMode) {
      Serial.print(F("[DROP] canali ricevuti: "));
      Serial.println(channel);
    }
    return;
  }

  packetsReceived++;
  lastPacketTime = millis();
  sendOSC();

  if (verboseMode) {
    Serial.print(F("[RX #"));
    Serial.print(packetsReceived);
    Serial.print(F("] "));
    for (int i = 0; i < NUM_CHANNELS; i++) {
      Serial.print((long)sensorValues[i]);
      if (i < NUM_CHANNELS - 1) Serial.print(',');
    }
    Serial.println();
  }

  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

// ════════════════════════════════════════════════════════════
// Invio messaggio OSC
// ════════════════════════════════════════════════════════════
void sendOSC() {
  if (WiFi.status() != WL_CONNECTED) return;

  OSCMessage msg("/etude");
  for (int i = 0; i < NUM_CHANNELS; i++) msg.add(sensorValues[i]);

  udp.beginPacket(OSC_TARGET_IP, OSC_PORT);
  msg.send(udp);
  udp.endPacket();
  msg.empty();

  packetsSent++;
}

// ════════════════════════════════════════════════════════════
// Pacchetto OSC con valore fisso (ping / zero)
// ════════════════════════════════════════════════════════════
void sendSpecialOSC(float fillValue) {
  for (int i = 0; i < NUM_CHANNELS; i++) sensorValues[i] = fillValue;
  sendOSC();
  Serial.print(F("[SENT] /etude tutti "));
  Serial.println((int)fillValue);
}

// ════════════════════════════════════════════════════════════
// Self-test: gaussiana rotante inviata via OSC
// ════════════════════════════════════════════════════════════
void generateAndSendTestData() {
  float phase = (float)(millis() % 5000) / 5000.0f * TWO_PI;

  for (int i = 0; i < NUM_CHANNELS; i++) {
    float angle     = (float)i / NUM_CHANNELS * TWO_PI;
    float diff      = angle - phase;
    float intensity = exp(-3.0f * diff * diff);
    sensorValues[i] = intensity * 500.0f + random(0, 10);
  }

  sendOSC();
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));

  if (verboseMode) {
    int maxCh = 0;
    for (int i = 1; i < NUM_CHANNELS; i++) {
      if (sensorValues[i] > sensorValues[maxCh]) maxCh = i;
    }
    Serial.print(F("[TEST #"));
    Serial.print(packetsSent);
    Serial.print(F("] peak @ ch"));
    Serial.print(maxCh + 1);
    Serial.print(F(" = "));
    Serial.println((int)sensorValues[maxCh]);
  }
}

// ════════════════════════════════════════════════════════════
// Comandi debug via Serial USB
// ════════════════════════════════════════════════════════════
void handleSerialCommands() {
  if (!Serial.available()) return;

  char cmd = Serial.read();

  switch (cmd) {
    case 't':
      selfTestMode = !selfTestMode;
      Serial.print(F("\n>> Self-test: "));
      Serial.println(selfTestMode ? F("ON") : F("OFF"));
      break;
    case 'i':
      printInfo();
      break;
    case 'p':
      Serial.println(F("\n>> PING"));
      sendSpecialOSC(999.0f);
      break;
    case 'z':
      Serial.println(F("\n>> ZERO"));
      sendSpecialOSC(0.0f);
      break;
    case 'v':
      verboseMode = !verboseMode;
      Serial.print(F("\n>> Verbose: "));
      Serial.println(verboseMode ? F("ON") : F("OFF"));
      break;
    case 'l':
      printLastPacket();
      break;
    case 'w':
      Serial.println(F("\n>> Riconnessione WiFi..."));
      WiFi.disconnect();
      delay(100);
      connectWiFi();
      break;
    case 'h':
      printHelp();
      break;
    default:
      break;
  }
}

// ════════════════════════════════════════════════════════════
// Info di stato
// ════════════════════════════════════════════════════════════
void printInfo() {
  unsigned long uptime = (millis() - bootTime) / 1000;

  Serial.println(F("\n══════════ STATO ESP32 ══════════"));

  Serial.print(F("WiFi: "));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("connesso a "));
    Serial.println(WIFI_SSID);
    Serial.print(F("  IP:   "));
    Serial.println(WiFi.localIP());
    Serial.print(F("  RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  } else {
    Serial.println(F("DISCONNESSO"));
  }
  Serial.print(F("  Riconnessioni: "));
  Serial.println(wifiReconnects);

  Serial.print(F("OSC → "));
  Serial.print(OSC_TARGET_IP);
  Serial.print(':');
  Serial.println(OSC_PORT);

  Serial.println(F("── Contatori ──"));
  Serial.print(F("  Ricevuti (OK):  ")); Serial.println(packetsReceived);
  Serial.print(F("  Inviati (OSC):  ")); Serial.println(packetsSent);
  Serial.print(F("  Scartati:       ")); Serial.println(packetsDropped);
  Serial.print(F("  Byte seriale:   ")); Serial.println(bytesReceived);

  Serial.println(F("── Timing ──"));
  Serial.print(F("  Uptime: "));
  Serial.print(uptime / 3600);          Serial.print(F("h "));
  Serial.print(uptime % 3600 / 60);     Serial.print(F("m "));
  Serial.print(uptime % 60);            Serial.println(F("s"));

  if (lastPacketTime > 0) {
    Serial.print(F("  Ultimo pkt: "));
    Serial.print((millis() - lastPacketTime) / 1000.0f, 1);
    Serial.println(F(" s fa"));
    if (packetsReceived > 0 && uptime > 0) {
      Serial.print(F("  Rate medio: "));
      Serial.print((float)packetsReceived / uptime, 1);
      Serial.println(F(" pkt/s"));
    }
  } else {
    Serial.println(F("  Nessun pacchetto ricevuto"));
  }

  Serial.print(F("Modo: "));
  Serial.println(selfTestMode ? F("SELF-TEST") : F("BRIDGE"));
  Serial.println(F("═════════════════════════════════"));
}

// ════════════════════════════════════════════════════════════
// Ultimo pacchetto ricevuto
// ════════════════════════════════════════════════════════════
void printLastPacket() {
  Serial.println(F("\n── Ultimo pacchetto ──"));
  if (packetsReceived == 0 && !selfTestMode) {
    Serial.println(F("  Nessun pacchetto ricevuto"));
    return;
  }
  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.print(F("  CH"));
    if (i + 1 < 10) Serial.print('0');
    Serial.print(i + 1);
    Serial.print(F(": "));
    Serial.println((long)sensorValues[i]);
  }
}

// ════════════════════════════════════════════════════════════
// Connessione WiFi
// ════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.print(F("Connessione a "));
  Serial.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print('.');
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    attempts++;
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connesso! IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
    digitalWrite(LED_PIN, HIGH);
    wifiReconnects++;
  } else {
    Serial.println(F("Connessione fallita, riprovo al prossimo ciclo"));
    digitalWrite(LED_PIN, LOW);
  }
}

// ════════════════════════════════════════════════════════════
// Help
// ════════════════════════════════════════════════════════════
void printHelp() {
  Serial.println(F("\n══════════ étude — COMANDI DEBUG ══════════"));
  Serial.println(F("  t — TEST    (self-test onda simulata → OSC)"));
  Serial.println(F("  i — INFO    (WiFi, contatori, uptime)"));
  Serial.println(F("  p — PING    (tutti i valori a 999)"));
  Serial.println(F("  z — ZERO    (tutti i valori a 0)"));
  Serial.println(F("  v — VERBOSE (toggle stampa ogni pacchetto)"));
  Serial.println(F("  l — LAST    (ultimo pacchetto ricevuto)"));
  Serial.println(F("  w — WIFI    (riconnessione forzata)"));
  Serial.println(F("  h — HELP    (questa lista)"));
  Serial.println(F("═══════════════════════════════════════════"));
}
