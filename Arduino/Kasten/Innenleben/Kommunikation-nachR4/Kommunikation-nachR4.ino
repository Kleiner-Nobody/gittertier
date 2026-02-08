/* * GITTERTIER PRO - KASTEN SENSOR HUB
 * Momentary Power Toggle & Hardware Scan
 */

const int pinsSw[] = {2, 3, 4, 5, 6, 7, 8, 9}; // Rotary Switches
const int pinPwr = 10; 
const int pinStop = 13;

bool isOn = false;
int lastPwrState = HIGH;
unsigned long lastDeb = 0;

void setup() {
  Serial.begin(115200);
  for(int i=0; i<8; i++) pinMode(pinsSw[i], INPUT_PULLUP);
  pinMode(pinPwr, INPUT_PULLUP);
  pinMode(pinStop, INPUT_PULLUP);
}

void loop() {
  // 1. Power Toggle Logic
  int reading = digitalRead(pinPwr);
  if (reading != lastPwrState) lastDeb = millis();
  if ((millis() - lastDeb) > 50) {
    if (reading == LOW && !isOn) {
      isOn = true; Serial.println("CMD|POWER_ON"); delay(500);
    } else if (reading == LOW && isOn) {
      isOn = false; Serial.println("CMD|POWER_OFF"); delay(500);
    }
  }
  lastPwrState = reading;

  // 2. Hardware Monitoring
  static int lastStop = -1;
  int stopVal = digitalRead(pinStop);
  if (stopVal != lastStop) {
    Serial.println(stopVal == HIGH ? "CMD|STOP_ON" : "CMD|STOP_OFF");
    lastStop = stopVal;
  }

  if (isOn) {
    static int lastS[8];
    for(int i=0; i<8; i++) {
      int v = digitalRead(pinsSw[i]);
      if (v != lastS[i]) {
        Serial.print("SW|"); Serial.print(pinsSw[i]); 
        Serial.print("|"); Serial.println(v == LOW ? "1" : "0");
        lastS[i] = v;
      }
    }
  }
  delay(10);
}