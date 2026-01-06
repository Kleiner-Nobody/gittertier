// --- SENDER CODE (Arduino Nano) ---
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Pin Definitionen laut deiner CSV 
const int n_csn = 10;
// Laut CSV ist GDO0 auf D3. Die Lib erwartet für TX nicht zwingend einen Interrupt,
// aber wir definieren es korrekt.
const int n_gdo0 = 3; 

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- GITTERTIER: NANO SENDER START ---");

  // FÜGE DIESE ZEILE HINZU (aus deinem Original-Code):
  // SCK=13, MISO=12, MOSI=11, CSN=10
  ELECHOUSE_cc1101.setSpiPin(13, 12, 11, n_csn); 

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("STATUS: CC1101 am Nano gefunden.");
  } else {
    Serial.println("ALARM: CC1101 am Nano nicht gefunden!");
    while(1);
  }
  
  ELECHOUSE_cc1101.Init();
  
  // Frequenz auf 433.92 MHz (Standard) setzen
  ELECHOUSE_cc1101.setMHZ(433.92);
  
  // Sendeleistung: -30 war in deinem Code[cite: 7]. 
  // Zum Testen erhöhe ich auf 0 oder 10, damit wir sicher Empfang haben.
  // Später kannst du wieder auf -30 runtergehen für RSSI Tests auf kurze Distanz.
  ELECHOUSE_cc1101.setPA(10); 
  
  ELECHOUSE_cc1101.setSyncMode(2);
  ELECHOUSE_cc1101.setCrc(1);
}

int counter = 0;

void loop() {
  // Nachricht vorbereiten
  String msg = "Ping " + String(counter);
  int len = msg.length();
  byte buffer[len + 1];
  msg.getBytes(buffer, len + 1);

  // Senden mit Library-Funktion (baut Header und CRC automatisch)
  ELECHOUSE_cc1101.SendData(buffer, len);
  
  Serial.print("TX (Nano): ");
  Serial.println(msg);
  
  counter++;
  delay(1000); // 1 Sekunde warten
}