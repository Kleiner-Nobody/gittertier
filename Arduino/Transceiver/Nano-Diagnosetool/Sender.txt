#include <ELECHOUSE_CC1101_SRC_DRV.h>

byte counter = 0;

void setup() {
  Serial.begin(9600);
  delay(2000);
  
  Serial.println("--- BLIND-SENDER START ---");

  // Wir initialisieren Pin 3 nicht mal, weil wir ihn ignorieren
  // ELECHOUSE_cc1101.setGDO(3, 0); 

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("SPI Verbindung OK.");
  } else {
    Serial.println("SPI Fehler.");
    while(1);
  }

  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(432.80); 
  ELECHOUSE_cc1101.setPA(-30); 
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setSyncWord(0xD3, 0x91);
  ELECHOUSE_cc1101.setDRate(2.4);
}

void loop() {
  // 1. Daten manuell in den Speicher (FIFO) schreiben
  byte data[1];
  data[0] = counter;
  ELECHOUSE_cc1101.SpiWriteBurstReg(CC1101_TXFIFO, data, 1);

  // 2. Befehl geben: "FEUER FREI!" (STX = Start Transmit)
  ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);

  // 3. WICHTIG: Wir warten einfach blind 20ms, statt auf Pin 3 zu warten
  delay(20);

  // 4. Zurück in den Standby (Sicher ist sicher)
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SIDLE);

  Serial.print("Blind gesendet: ");
  Serial.println(counter);
  
  counter++;
  delay(1000);
}