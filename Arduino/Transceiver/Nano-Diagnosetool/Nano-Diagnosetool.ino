#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <SPI.h>

const int n_csn = 10;
const int n_mosi = 11;
const int n_miso = 12;
const int n_sck = 13;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--- CC1101 TIEFENDIAGNOSE (NANO) ---");

  // 1. SPI manuell starten, um sicherzugehen
  SPI.begin();
  pinMode(n_csn, OUTPUT);
  digitalWrite(n_csn, HIGH); // CSN inaktiv (High)

  Serial.println("Schritt 1: Pins konfiguriert.");
  
  // Library Setup
  ELECHOUSE_cc1101.setSpiPin(n_sck, n_miso, n_mosi, n_csn);

  // 2. Wir resetten das Modul manuell
  Serial.println("Schritt 2: Versuche Software-Reset...");
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SRES);
  delay(100);

  // 3. Register direkt auslesen
  // Wir lesen Register 0x31 (VERSION) und 0x30 (PARTNUM)
  // Das sind Read-Only Werte, die fest im Chip eingebrannt sind.
  
  Serial.println("Schritt 3: Lese Chip-Register...");
  
  byte version = ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION);
  byte partnum = ELECHOUSE_cc1101.SpiReadStatus(CC1101_PARTNUM);
  
  Serial.print("-> Gelesen VERSION (Hex): 0x");
  Serial.println(version, HEX);
  
  Serial.print("-> Gelesen PARTNUM (Hex): 0x");
  Serial.println(partnum, HEX);

  Serial.println("\n--- ANALYSE ---");
  
  if (version == 0x00) {
    Serial.println("ERGEBNIS: 0x00 (Alles Nullen)");
    Serial.println("URSACHE: Verbindung zu MISO (Pin 12) unterbrochen oder Kurzschluss nach GND.");
    Serial.println("TIPPS: Prüfe Kabel an Pin 12. Hat das Modul Strom?");
  } 
  else if (version == 0xFF) {
    Serial.println("ERGEBNIS: 0xFF (Alles Einsen)");
    Serial.println("URSACHE: Modul antwortet nicht. MISO (Pin 12) 'schwebt' oder VCC fehlt.");
    Serial.println("TIPPS: Prüfe GND und VCC (3.3V). Prüfe, ob CSN (Pin 10) wirklich Kontakt hat.");
  } 
  else if (version == 0x14 || version == 0x04) {
    Serial.println("ERGEBNIS: Erfolgreich! (Wert 0x14 oder 0x04 ist korrekt)");
    Serial.println("INFO: Der Chip antwortet korrekt. Das Problem lag evtl. an der Library-Initialisierung.");
  } 
  else {
    Serial.print("ERGEBNIS: Unbekannter Wert (0x");
    Serial.print(version, HEX);
    Serial.println(")");
    Serial.println("URSACHE: Störungen auf der Leitung (Wackelkontakt) oder defekter Chip.");
  }
}

void loop() {
  // Nichts zu tun
}