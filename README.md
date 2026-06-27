# étude. un'indagine sull'integrazione di sistemi interattivi per il cerchio aereo

**étude.** è un progetto di ricerca che esplora la relazione tra corpo, movimento e dato digitale attraverso la pratica del circo contemporaneo. Il progetto trasforma il cerchio aereo in un'interfaccia attiva capace di mappare il tocco della performer e tradurlo in segnali digitali in tempo reale per la generazione di contenuti visuali e sonori.

## Panoramica del Progetto
La ricerca si basa su un approccio autoetnografico e sulla pratica artistica personale. L'obiettivo è indagare come l'integrazione di sistemi interattivi possa espandere le possibilità drammaturgiche, trasformando l'apparato aereo in un partner reattivo che smette di essere un oggetto statico per diventare parte integrante del processo creativo.

## Struttura del Repository
```text
ètude/
├── documentazione/
│   └── etude_ricerca.pdf   # Tesi completa (ricerca teorica e progettuale)
├── hardware/
│   ├── etude_mega/         # Codice Arduino Mega (rilevamento 24 canali)
│   ├── etude_esp32/        # Codice ESP32 (ponte OSC via WiFi UDP)
│   ├── schema/             # Schema dei collegamenti elettronici
│   ├── .gitignore          # Esclusione file di configurazione sensibili
│   ├── config.example.h    # Template per credenziali WiFi e IP
│   └── config.h            # (Locale) Credenziali personali (non incluso)
└── software/
    └── etude.toe           # Progetto TouchDesigner
```

## Architettura

### Hardware
* **Arduino Mega 2560**: Gestisce la scansione di 24 sensori capacitivi disposti ad anello sull'attrezzo.
* **ESP32 S3**: Riceve i dati via seriale da Arduino e li trasmette via protocollo OSC (Open Sound Control) tramite WiFi UDP.
* **Connessioni**: Il TX di Arduino (5V) è collegato all'RX dell'ESP32 (3.3V) tramite un partitore di tensione (1kΩ/2kΩ) per garantire la sicurezza dei componenti.

### Software
* **Arduino IDE**: Il progetto richiede l'installazione delle librerie CapacitiveSensor e OSC.
* **TouchDesigner**: È necessaria la versione 2023.10000 o superiore per supportare i nodi POP (Point Operators) utilizzati nel progetto.
* **Configurazione OSC**: All'interno del file .toe, nell'operatore OSC In CHOP, è necessario modificare il campo "Local Address" inserendo l'indirizzo IP locale del proprio computer per ricevere i dati dall'ESP32 sulla porta UDP 9000.
* **Nota Licenza**: Il progetto utilizza un componente .tox di DotSimulate (Patreon). Per motivi di copyright, il file non è incluso nel repository. È necessario ottenere il componente originale e inserirlo nel modulo dedicato all'interno del file di progetto.

## Installazione e Configurazione
1. Entrare nella cartella hardware.
2. Copiare il file config.example.h e rinominarlo in config.h.
3. Inserire SSID, password Wi-Fi e l'indirizzo IP del computer di destinazione nel file config.h.
4. Caricare i codici nelle rispettive schede Arduino Mega ed ESP32 S3.

## Strumenti di Debug
Entrambi i dispositivi supportano comandi tramite Serial Monitor (baud rate 115200):
* **t (Test)**: Avvia un self-test che simula un'onda sinusoidale rotante sui 24 canali.
* **s (Stats)**: (Disponibile su Arduino) Visualizza le statistiche min/max e medie per ogni sensore.
* **i (Info)**: (Disponibile su ESP32) Mostra lo stato del WiFi, l'IP locale e i contatori di pacchetti.
* **v (Verbose)**: Attiva la stampa dettagliata dei pacchetti ricevuti.
