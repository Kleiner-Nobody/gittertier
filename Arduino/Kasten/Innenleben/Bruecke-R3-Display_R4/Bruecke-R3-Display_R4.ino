#include <ArduinoBLE.h>
#include <Arduino_LED_Matrix.h>

ArduinoLEDMatrix matrix;
BLECharacteristic targetChar;
bool isAuth = false;

void setup() {
  Serial.begin(115200);  // Zum PC (Debug)
  Serial1.begin(115200); // Zum R3 (Pins 0/1)
  matrix.begin();
  
  if (!BLE.begin()) { Serial.println("BLE Fail!"); while(1); }
  BLE.scanForUuid("19b10000-e8f2-537e-4f6c-d104768a1214");
  Serial.println("Suche Display...");
}

void loop() {
  if (!BLE.connected()) {
    isAuth = false;
    BLEDevice p = BLE.available();
    if (p && p.localName() == "Gittertier Pro") {
      if (p.connect() && p.discoverAttributes()) {
        targetChar = p.characteristic("19b10001-e8f2-537e-4f6c-d104768a1214");
        if (targetChar) {
          targetChar.writeValue("AUTH|GITTER_77_PRO_SEC");
          isAuth = true;
          Serial.println(">>> Display verifiziert!");
        }
      }
    }
  }

  // DEBUG-PASSTHROUGH: Alles vom R3 an PC und Display schicken
  if (Serial1.available()) {
    String r3Data = Serial1.readStringUntil('\n');
    r3Data.trim();
    
    Serial.print("Vom R3 empfangen: "); Serial.println(r3Data);
    
    if (isAuth) {
      targetChar.writeValue(r3Data.c_str());
      showPulse();
    }
  }
}

void showPulse() {
  uint8_t f[8][12] = {0}; f[0][0]=1; matrix.renderBitmap(f,8,12); delay(10);
  f[0][0]=0; matrix.renderBitmap(f,8,12);
}