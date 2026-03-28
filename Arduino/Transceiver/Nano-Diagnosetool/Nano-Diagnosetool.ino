/*
 * GITTERTIER PRO - KASTEN SENSOR-HUB (UNO R3)
 * Liest 11 Eingänge und sendet sie via Serial an den R4.
 */

const int schalter[] = {2, 3, 4, 5, 6, 7, 8, 9, 10}; // Drehschalter & Power
const int pinNotAus = 13; // Not-Aus (Öffner)

void setup() {
  Serial.begin(115200); // Verbindung zum R4
  for(int i=0; i<9; i++) pinMode(schalter[i], INPUT_PULLUP);
  pinMode(pinNotAus, INPUT_PULLUP);
}

void loop() {
  static int lastStates[14];
  
  // 1. Not-Aus Check (Höchste Prio)
  int stopState = digitalRead(pinNotAus);
  if(stopState != lastStates[pinNotAus]) {
    Serial.println(stopState == HIGH ? "BOX|STOP" : "BOX|OK");
    lastStates[pinNotAus] = stopState;
  }

  // 2. Schalter & Taster Check
  for(int i=0; i<9; i++) {
    int val = digitalRead(schalter[i]);
    if(val == LOW && lastStates[schalter[i]] == HIGH) {
      Serial.print("BTN|"); Serial.println(schalter[i]);
    }
    lastStates[schalter[i]] = val;
  }
  delay(30); // Entprellen
}