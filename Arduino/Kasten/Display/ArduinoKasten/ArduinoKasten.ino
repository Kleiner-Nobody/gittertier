#include <Arduino_LED_Matrix.h>
#include <ArduinoBLE.h>

/* --- HARDWARE MAPPING --- */
const int PIN_PWR = 2;       // Einschalter
const int PIN_EMERGENCY = 3; // Not-Aus
const int J_UP=4, J_DOWN=5, J_LEFT=6, J_RIGHT=7; // Joystick
const int B1=8, B2=9, B3=10, B4=11, B5=12;       // Funktions-Buttons

ArduinoLEDMatrix matrix;
BLEDevice displayDevice;
BLECharacteristic commandChar;

const char* AUTH_ID = "GITTER_77_PRO_SEC";
bool authenticated = false;

void setup() {
  matrix.begin();
  for(int i=2; i<=12; i++) pinMode(i, INPUT_PULLUP);
  if (!BLE.begin()) while(1);
  
  BLE.scanForUuid("19b10000-e8f2-537e-4f6c-d104768a1214");
}

void loop() {
  if (!BLE.connected()) {
    authenticated = false;
    drawMatrixLoading(100); // Suche
    
    BLEDevice peripheral = BLE.available();
    if (peripheral && peripheral.localName() == "Gittertier Pro") {
      BLE.stopScan();
      if (peripheral.connect() && peripheral.discoverAttributes()) {
        commandChar = peripheral.characteristic("19b10001-e8f2-537e-4f6c-d104768a1214");
        if (commandChar) {
          commandChar.writeValue(AUTH_ID);
          authenticated = true;
        }
      } else { BLE.scan(); }
    }
  } else {
    handleHardware();
  }
}

void handleHardware() {
  static bool lastEm = false;
  bool emergency = (digitalRead(PIN_EMERGENCY) == HIGH); // Not-Aus ausgelöst?

  if(emergency != lastEm) {
    commandChar.writeValue(emergency ? "ALRM|ON" : "ALRM|OFF");
    lastEm = emergency;
  }

  if(!emergency) {
    static int lastStates[13];
    for(int i=2; i<=12; i++) {
      int val = digitalRead(i);
      if(val == LOW && lastStates[i] == HIGH) {
        String cmd = "BTN|" + String(i);
        commandChar.writeValue(cmd.c_str());
      }
      lastStates[i] = val;
    }
  }
}

void drawMatrixLoading(int speed) {
  static int x=0, y=0;
  uint8_t m[8][12] = {0}; m[y][x] = 1;
  matrix.renderBitmap(m, 8, 12);
  x++; if(x>=12){x=0; y++;} if(y>=8){y=0;}
  delay(speed);
}