#include <CapacitiveSensor.h>
#include "config.h"

/*
  étude — Arduino Mega
  24 sensori capacitivi disposti ad anello → ESP32 via Serial1

  Formato pacchetto: $v1,v2,...,v24\n
    '$'    — inizio pacchetto
    v1…v24 — valori interi separati da virgola
    '\n'   — fine pacchetto

  ── Pinout sensori ──────────────────────────────
  CH1  → pin 22    CH13 → pin 34
  CH2  → pin 23    CH14 → pin 35
  CH3  → pin 24    CH15 → pin 36
  CH4  → pin 25    CH16 → pin 37
  CH5  → pin 26    CH17 → pin 38
  CH6  → pin 27    CH18 → pin 39
  CH7  → pin 28    CH19 → pin 40
  CH8  → pin 29    CH20 → pin 41
  CH9  → pin 30    CH21 → pin 42
  CH10 → pin 31    CH22 → pin 43
  CH11 → pin 32    CH23 → pin 44
  CH12 → pin 33    CH24 → pin 45

  ── Collegamento Arduino → ESP32 ────────────────
  TX1 (pin 18) → 1kΩ → ESP32 RX2 (GPIO16)
  Partitore 5V→3.3V: 2kΩ dal nodo a GND
  GND comune

  ── Comandi debug (Serial Monitor USB) ──────────
  t — TEST:  dati simulati (onda sinusoidale rotante)
  r — REAL:  lettura reale dei sensori
  s — STATS: min/max/media per ogni canale
  p — PING:  pacchetto con tutti i valori a 999
  z — ZERO:  pacchetto con tutti i valori a 0
  d — DEBUG: toggle stampa dettagliata su USB
  h — HELP:  mostra questa lista
*/

// ── Configurazione ───────────────────────────────────────────
const int SEND_PIN     = 5;
const int NUM_CHANNELS = 24;
const int RECEIVE_PINS[NUM_CHANNELS] = {
  22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
  34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45
};
const int NUM_SAMPLES = 15;
const int TIMEOUT_MS  = 20;

// ── Oggetti sensore ──────────────────────────────────────────
CapacitiveSensor* sensors[NUM_CHANNELS];

// ── Stato ────────────────────────────────────────────────────
char          txBuffer[256];
bool          testMode      = false;
bool          debugPrint    = false;
unsigned long frameCount    = 0;
unsigned long loopStartTime = 0;

// ── Statistiche ──────────────────────────────────────────────
long          statMin[NUM_CHANNELS];
long          statMax[NUM_CHANNELS];
long          statSum[NUM_CHANNELS];
unsigned long statCount = 0;


// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  for (int i = 0; i < NUM_CHANNELS; i++) {
    sensors[i] = new CapacitiveSensor(SEND_PIN, RECEIVE_PINS[i]);
    sensors[i]->set_CS_AutocaL_Millis(0xFFFFFFFF);
    sensors[i]->set_CS_Timeout_Millis(TIMEOUT_MS);
  }

  resetStats();

  Serial.println(F("═══════════════════════════════════════"));
  Serial.println(F("  étude — 24ch capacitive ring"));
  Serial.println(F("  Invia 'h' per i comandi debug"));
  Serial.println(F("═══════════════════════════════════════"));
  delay(1000);

  loopStartTime = millis();
}

// ════════════════════════════════════════════════════════════
void loop() {
  handleSerialCommands();

  long values[NUM_CHANNELS];

  if (testMode) {
    generateTestData(values);
  } else {
    readSensors(values);
  }

  updateStats(values);
  buildPacket(values);

  Serial1.print(txBuffer);

  if (debugPrint) {
    printDebugDetailed(values);
  } else {
    Serial.print(txBuffer);
  }

  frameCount++;
  delay(50);  // ~20 Hz
}


// ════════════════════════════════════════════════════════════
// Lettura reale dei sensori
// ════════════════════════════════════════════════════════════
void readSensors(long* values) {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    values[i] = sensors[i]->capacitiveSensor(NUM_SAMPLES);

    if (values[i] < 0) {  // timeout o errore: reset pin
      values[i] = 0;
      pinMode(RECEIVE_PINS[i], OUTPUT);
      digitalWrite(RECEIVE_PINS[i], LOW);
      pinMode(RECEIVE_PINS[i], INPUT);
    }
  }
}

// ════════════════════════════════════════════════════════════
// Dati di test: gaussiana che ruota lungo l'anello
// ════════════════════════════════════════════════════════════
void generateTestData(long* values) {
  float phase = (float)(millis() % 5000) / 5000.0f * TWO_PI;

  for (int i = 0; i < NUM_CHANNELS; i++) {
    float angle     = (float)i / NUM_CHANNELS * TWO_PI;
    float diff      = angle - phase;
    float intensity = exp(-3.0f * diff * diff);
    values[i] = (long)(intensity * 500.0f) + random(0, 10);
  }
}

// ════════════════════════════════════════════════════════════
// Costruzione pacchetto: $v1,v2,...,v24\n
// ════════════════════════════════════════════════════════════
void buildPacket(long* values) {
  int pos = 0;
  txBuffer[pos++] = '$';

  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (i > 0) txBuffer[pos++] = ',';
    char numStr[12];
    ltoa(values[i], numStr, 10);
    int len = strlen(numStr);
    memcpy(&txBuffer[pos], numStr, len);
    pos += len;
  }

  txBuffer[pos++] = '\n';
  txBuffer[pos]   = '\0';
}

// ════════════════════════════════════════════════════════════
// Pacchetto speciale (ping / zero)
// ════════════════════════════════════════════════════════════
void sendSpecialPacket(long fillValue) {
  long values[NUM_CHANNELS];
  for (int i = 0; i < NUM_CHANNELS; i++) values[i] = fillValue;
  buildPacket(values);
  Serial1.print(txBuffer);
  Serial.print(F("[SPECIAL] "));
  Serial.print(txBuffer);
}

// ════════════════════════════════════════════════════════════
// Comandi debug via Serial USB
// ════════════════════════════════════════════════════════════
void handleSerialCommands() {
  if (!Serial.available()) return;

  char cmd = Serial.read();

  switch (cmd) {
    case 't':
      testMode = true;
      Serial.println(F("\n>> MODO TEST — dati simulati (onda rotante)"));
      break;
    case 'r':
      testMode = false;
      Serial.println(F("\n>> MODO REALE — lettura sensori"));
      break;
    case 's':
      printStats();
      break;
    case 'p':
      Serial.println(F("\n>> PING — pacchetto tutti 999"));
      sendSpecialPacket(999);
      break;
    case 'z':
      Serial.println(F("\n>> ZERO — pacchetto tutti 0"));
      sendSpecialPacket(0);
      break;
    case 'd':
      debugPrint = !debugPrint;
      Serial.print(F("\n>> Debug dettagliato: "));
      Serial.println(debugPrint ? F("ON") : F("OFF"));
      break;
    case 'h':
      printHelp();
      break;
    default:
      break;
  }
}

// ════════════════════════════════════════════════════════════
// Stampa dettagliata per debug (barra visuale ASCII)
// ════════════════════════════════════════════════════════════
void printDebugDetailed(long* values) {
  Serial.print(F("frame:"));
  Serial.print(frameCount);

  float elapsed = (millis() - loopStartTime) / 1000.0f;
  if (elapsed > 0) {
    Serial.print(F(" fps:"));
    Serial.print(frameCount / elapsed, 1);
  }

  Serial.print(testMode ? F(" [TEST] ") : F(" [REAL] "));

  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.print(F("CH"));
    if (i + 1 < 10) Serial.print('0');
    Serial.print(i + 1);
    Serial.print(':');

    int bars = constrain(map(values[i], 0, 500, 0, 10), 0, 10);
    for (int b = 0;    b < bars; b++) Serial.print('#');
    for (int b = bars; b < 10;   b++) Serial.print('.');

    Serial.print(' ');
    Serial.print(values[i]);
    Serial.print('\t');
  }
  Serial.println();
}

// ════════════════════════════════════════════════════════════
// Statistiche
// ════════════════════════════════════════════════════════════
void resetStats() {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    statMin[i] = 999999;
    statMax[i] = 0;
    statSum[i] = 0;
  }
  statCount = 0;
}

void updateStats(long* values) {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (values[i] < statMin[i]) statMin[i] = values[i];
    if (values[i] > statMax[i]) statMax[i] = values[i];
    statSum[i] += values[i];
  }
  statCount++;
}

void printStats() {
  Serial.println(F("\n══════════ STATISTICHE SENSORI ══════════"));
  Serial.print(F("Campioni: "));
  Serial.println(statCount);

  float elapsed = (millis() - loopStartTime) / 1000.0f;
  Serial.print(F("Tempo:    "));
  Serial.print(elapsed, 1);
  Serial.println(F(" s"));

  if (elapsed > 0) {
    Serial.print(F("FPS medio: "));
    Serial.print(frameCount / elapsed, 1);
    Serial.println(F(" Hz"));
  }

  Serial.println(F("CH\tMIN\tMAX\tMEDIA\tRANGE"));
  Serial.println(F("──\t───\t───\t─────\t─────"));

  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.print(i + 1);
    Serial.print('\t');
    Serial.print(statMin[i]);
    Serial.print('\t');
    Serial.print(statMax[i]);
    Serial.print('\t');
    Serial.print(statCount > 0 ? statSum[i] / (long)statCount : 0);
    Serial.print('\t');
    Serial.println(statMax[i] - statMin[i]);
  }

  Serial.println(F("═════════════════════════════════════════"));
  Serial.println(F("(riavvia per resettare le statistiche)"));
}

// ════════════════════════════════════════════════════════════
// Help
// ════════════════════════════════════════════════════════════
void printHelp() {
  Serial.println(F("\n══════════ étude — COMANDI DEBUG ══════════"));
  Serial.println(F("  t — TEST  (dati simulati, onda rotante)"));
  Serial.println(F("  r — REAL  (lettura sensori)"));
  Serial.println(F("  s — STATS (min/max/media per canale)"));
  Serial.println(F("  p — PING  (tutti i valori a 999)"));
  Serial.println(F("  z — ZERO  (tutti i valori a 0)"));
  Serial.println(F("  d — DEBUG (toggle barra visuale)"));
  Serial.println(F("  h — HELP  (questa lista)"));
  Serial.println(F("═══════════════════════════════════════════"));
}
