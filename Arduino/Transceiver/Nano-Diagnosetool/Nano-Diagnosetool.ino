#include <ELECHOUSE_CC1101_SRC_DRV.h>

void setup() {
  Serial.begin(115200);
  delay(2000); // Zeit für den Nano-Start
  
  Serial.println("NANO DIAGNOSE START...");

  // Wir versuchen es mit Pin 10
  ELECHOUSE_cc1101.setSpiPin(13, 12, 11, 10);

  // Wir testen den Chip mehrfach
  bool found = false;
  for(int i=0; i<5; i++) {
    Serial.print("Versuch "); Serial.print(i+1); Serial.print(": ");
    if (ELECHOUSE_cc1101.getCC1101()) {
      found = true;
      Serial.println("GEFUNDEN!");
      break;
    } else {
      Serial.println("nicht da...");
    }
    delay(500);
  }

  if (found) {
    ELECHOUSE_cc1101.Init();
    Serial.println("Initialisierung erfolgreich!");
  } else {
    Serial.println("Kabel an D10, D11, D12, D13 prüfen!");
  }
}

void loop() {}