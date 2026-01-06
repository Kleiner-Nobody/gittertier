// --- EMPFÄNGER CODE (Arduino UNO R4 WiFi) ---
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Pin Definitionen laut deiner CSV 
const int r_csn = 10;
const int r_gdo0 = 2; // GDO0 an Pin 2 ist perfekt für Interrupts

void setup() {
  Serial.begin(115200);
  delay(2000); // Kurz warten, damit der Serial Monitor bereit ist
  Serial.println("\n--- GITTERTIER: UNO R4 EMPFAENGER START ---");

  // SPI Pins sind beim R4 auf dem ICSP Header bzw. 11/12/13 gemappt.
  // Wir nutzen die Standard-Belegung, setzen aber CSN und GDO explizit.
  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("STATUS: CC1101 am UNO R4 gefunden.");
  } else {
    Serial.println("ALARM: CC1101 nicht gefunden! Verkabelung prüfen.");
    while (1);
  }

  // Initialisierung
  ELECHOUSE_cc1101.Init();
  
  // Frequenz: Wir nutzen 433.92 MHz als Standard. 
  // Dein Sender Code nutzte 432.80[cite: 7], wir gleichen beide hier auf Standard an.
  ELECHOUSE_cc1101.setMHZ(433.92); 
  
  // Sync Mode für saubere Pakete
  ELECHOUSE_cc1101.setSyncMode(2); 
  
  // GDO0 auf Pin 2 konfigurieren
  ELECHOUSE_cc1101.setGDO(r_gdo0, 0);
  
  // Deaktiviert CRC Check nicht, wir wollen saubere Daten
  ELECHOUSE_cc1101.setCrc(1);

  Serial.println("Warte auf Daten...");
}

void loop() {
  // Prüfen, ob Daten empfangen wurden
  if (ELECHOUSE_cc1101.CheckReceiveFlag()) {
    
    // Daten abrufen
    byte buffer[64] = {0};
    byte len = ELECHOUSE_cc1101.ReceiveData(buffer);

    if (len > 0) {
      Serial.print("EMPFANGEN: ");
      // Wir erwarten Text oder Zahlen. Hier geben wir es als Text aus:
      Serial.print((char*)buffer);
      
      // RSSI (Signalstärke) und LQI (Qualität) ausgeben - WICHTIG für deine Triangulation
      Serial.print(" | RSSI: ");
      Serial.print(ELECHOUSE_cc1101.getRssi());
      Serial.print(" dBm | LQI: ");
      Serial.println(ELECHOUSE_cc1101.getLqi());
    }
    
    // Empfangsflag zurücksetzen
    ELECHOUSE_cc1101.SpiStrobe(CC1101_SIDLE);
    ELECHOUSE_cc1101.SpiStrobe(CC1101_SRX);
  }
}