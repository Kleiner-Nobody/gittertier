#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Deine Verkabelung laut CSV (UNO R4)
const int r_csn = 10;
const int r_mosi = 11;
const int r_miso = 12;
const int r_sck = 13;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--- CC1101 TIEFENDIAGNOSE (EMPFAENGER R4) ---");

  // WICHTIG: Wir zwingen den R4, die Pins 11-13 zu nutzen!
  // Ohne diesen Befehl sucht der R4 oft am falschen Anschluss (ICSP).
  ELECHOUSE_cc1101.setSpiPin(r_sck, r_miso, r_mosi, r_csn);

  // Chip Reset
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SRES);
  delay(100);

  // Register lesen
  byte version = ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION);
  byte partnum = ELECHOUSE_cc1101.SpiReadStatus(CC1101_PARTNUM);
  
  Serial.print("-> VERSION (Hex): 0x");
  Serial.println(version, HEX);
  Serial.print("-> PARTNUM (Hex): 0x");
  Serial.println(partnum, HEX);

  if (version == 0x14 || version == 0x04) {
    Serial.println("VERBINDUNG: PERFEKT!");
    Serial.println("Info: Der Chip ist erreichbar. Wenn er nichts empfängt, liegt es an Frequenz oder GDO0-Pin.");
  } else {
    Serial.println("VERBINDUNG: FEHLERHAFT!");
    if (version == 0x00) Serial.println("Tipp: Wahrscheinlich MOSI/MISO vertauscht (wie beim Nano).");
    if (version == 0xFF) Serial.println("Tipp: Wahrscheinlich falsche Pins benutzt (R4 sucht am ICSP).");
  }
}

void loop() {}