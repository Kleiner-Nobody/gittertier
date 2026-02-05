#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Standard-Pins für einen heilen Nano
const int n_csn = 10;
const int n_gdo0 = 3; 

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- NEUER NANO: FRISCHSTART ---");

  // Explizite Zuweisung der Standard-Pins (Sicherheitshalber)
  // SCK=13, MISO=12, MOSI=11, CSN=10
  ELECHOUSE_cc1101.setSpiPin(13, 12, 11, n_csn);

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("STATUS: HARDWARE OK! (Verbindung steht)");
  } else {
    Serial.println("ALARM: Verbindung fehlgeschlagen.");
    // Wenn das hier kommt, ist entweder das Breadboard kaputt 
    // oder die Kabel sind locker.
    while(1);
  }

  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setGDO(n_gdo0, 0); 
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.setSyncMode(2);
  ELECHOUSE_cc1101.setPA(10); 
  ELECHOUSE_cc1101.setAdrChk(0);
  
  Serial.println("Setup fertig. Senden startet...");
}

int counter = 0;

void loop() {
  String text = "NewNano " + String(counter);
  int len = text.length();
  byte buffer[len + 1];
  text.getBytes(buffer, len + 1);

  Serial.print("Sende: " + text + " ... ");
  ELECHOUSE_cc1101.SendData(buffer, len);
  Serial.println("OK!");
  
  counter++;
  delay(1000);
}