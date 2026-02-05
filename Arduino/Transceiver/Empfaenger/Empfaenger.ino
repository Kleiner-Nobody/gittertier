#include <ELECHOUSE_CC1101_SRC_DRV.h>

// PIN-KONFIGURATION UNO R4
const int r_csn = 10;
const int r_gdo0 = 2; // GDO0 an Pin 2

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--- 2. EMPFAENGER DEBUG (UNO R4) ---");

  // SPI Fix für R4
  ELECHOUSE_cc1101.setSpiPin(13, 12, 11, r_csn);
  
  // Wir konfigurieren Pin 2 als Eingang, um ihn zu überwachen
  pinMode(r_gdo0, INPUT);

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("Hardware: OK");
  } else {
    Serial.println("Hardware: FEHLER");
    while(1);
  }

  // --- HARD CONFIG (Identisch zum Sender!) ---
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.setSyncMode(2);
  ELECHOUSE_cc1101.setCrc(1); // CRC Check an (filtert kaputte Pakete)
  
  // GDO0 Konfiguration: Das Modul soll Pin 2 auf HIGH ziehen, wenn ein Paket da ist
  ELECHOUSE_cc1101.setGDO(r_gdo0, 0); 

  Serial.println("Warte auf Signale... (Beobachte RSSI)");
  
  // Empfangsmodus aktivieren
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SRX);
}

void loop() {
  // 1. Prüfen ob Daten da sind (via Library)
  if (ELECHOUSE_cc1101.CheckReceiveFlag()) {
    byte buffer[100] = {0};
    byte len = ELECHOUSE_cc1101.ReceiveData(buffer);

    if (len > 0) {
      Serial.print(">>> PAKET EMPFANGEN: ");
      Serial.print((char*)buffer);
      Serial.print(" | RSSI: ");
      Serial.println(ELECHOUSE_cc1101.getRssi());
    }
    // Nach Empfang sofort wieder lauschen
    ELECHOUSE_cc1101.SpiStrobe(CC1101_SRX);
  }

  // 2. DEBUG-Monitor (Alle 500ms): Zeigt, ob der Äther "lebt"
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 500) {
    lastCheck = millis();
    
    int rssi = ELECHOUSE_cc1101.getRssi();
    int gdoState = digitalRead(r_gdo0);
    
    // Wir geben nur Status aus, wenn KEIN Paket kam, damit der Monitor nicht flutet
    // RSSI Werte um -100 sind "Stille". Werte um -50 sind "Laut".
    Serial.print("[Monitor] RSSI: ");
    Serial.print(rssi);
    Serial.print(" dBm | GDO0-Pin Zustand: ");
    Serial.println(gdoState);
  }
}