#include <ELECHOUSE_CC1101_SRC_DRV.h>

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\n--- ZEITLUPE MIT PIN 4 START ---");
  
  // WICHTIG: Wir sagen der Library, dass CSN jetzt an Pin 4 ist!
  // (SCK, MISO, MOSI bleiben automatisch auf 13, 12, 11)
  ELECHOUSE_cc1101.setSpiPin(13, 12, 11, 4); 

  delay(500);
  Serial.print("1. Setze Pin Config (GDO0 auf 3)... ");
  ELECHOUSE_cc1101.setGDO(3, 0);
  Serial.println("OK");
  Serial.flush();

  delay(500);
  Serial.print("2. Hardware Check (über Pin 4)... ");
  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("OK (Chip da)");
  } else {
    Serial.println("FEHLER (Kein Chip)");
    Serial.println("Hast du das Kabel wirklich in Pin 4 gesteckt?");
    while(1);
  }
  Serial.flush();

  delay(500);
  Serial.print("3. Init (Reset Chip)... ");
  // Hier ist er vorhin abgestürzt
  ELECHOUSE_cc1101.Init();
  Serial.println("OK - HABEN WIR ÜBERLEBT?");
  Serial.flush();
  
  delay(500);
  Serial.print("4. Frequenz 432.80... ");
  ELECHOUSE_cc1101.setMHZ(432.80);
  Serial.println("OK");
  
  ELECHOUSE_cc1101.setPA(-30);
  ELECHOUSE_cc1101.setModulation(2);
  
  Serial.println("5. Schalte auf RX...");
  ELECHOUSE_cc1101.SetRx();
  Serial.println("--- SYSTEM LÄUFT STABIL ---");
}

void loop() {
  // Wenn wir hier ankommen, haben wir gewonnen.
  // Jetzt schauen wir nur noch nach RSSI
  Serial.print("RSSI: ");
  Serial.println(ELECHOUSE_cc1101.getRssi());
  delay(500);
}