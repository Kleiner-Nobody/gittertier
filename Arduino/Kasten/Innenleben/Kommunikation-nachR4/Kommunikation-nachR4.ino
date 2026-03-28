/**
 * GITTERTIER PRO - SENSOR HUB (R3) - v16.1 (FIXED POWER LOGIC)
 * Problem behoben: Kein Spammen von REQ_OFF mehr.
 */

#include <Arduino.h>

const uint8_t PIN_SW[]      = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t PIN_POWER     = 10;
const uint8_t PIN_EMERGENCY = 13;

// --- STATE MANAGEMENT ---
enum State { S_OFF, S_BOOT, S_ACTIVE };
State currentState = S_OFF;

// Entprellung & Timer
unsigned long lastPowerPress = 0;
const unsigned long POWER_COOLDOWN = 2000; // 2 Sekunden Sperre nach Druck

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 8; i++) pinMode(PIN_SW[i], INPUT_PULLUP);
  pinMode(PIN_POWER, INPUT_PULLUP);
  pinMode(PIN_EMERGENCY, INPUT_PULLUP);
  
  delay(500);
}

void sendCmd(String type, String id, String val) {
  // Format: [TYPE:ID:VAL]
  Serial.print("[");
  Serial.print(type); Serial.print(":");
  Serial.print(id);   Serial.print(":");
  Serial.print(val);
  Serial.println("]");
}

void loop() {
  unsigned long now = millis();

  // 1. POWER BUTTON LOGIK (Momentary)
  // Wir lesen den Taster. LOW = Gedrückt.
  if (digitalRead(PIN_POWER) == LOW) {
    
    // Nur reagieren, wenn Cooldown vorbei ist
    if (now - lastPowerPress > POWER_COOLDOWN) {
      
      if (currentState == S_OFF) {
        // System starten
        currentState = S_BOOT;
        sendCmd("SYS", "PWR", "START");
        lastPowerPress = now;
        
      } else if (currentState == S_ACTIVE) {
        // System beenden wollen
        // Wir ändern den Status NICHT auf OFF, das macht nur das Display visuell.
        // Aber wir senden den Wunsch.
        sendCmd("SYS", "PWR", "REQ_OFF");
        
        // Trick: Wir setzen den Status auf S_OFF, damit beim nächsten Drücken
        // wieder "START" gesendet wird. Das Display entscheidet, ob es wirklich ausgeht.
        currentState = S_OFF; 
        lastPowerPress = now;
      }
    }
  }

  // Automatischer Wechsel von BOOT zu ACTIVE nach 3 Sekunden
  if (currentState == S_BOOT && (now - lastPowerPress > 3000)) {
    currentState = S_ACTIVE;
  }

  // 2. NOT-AUS (Immer Vorrang)
  static int lastStop = -1;
  int stopVal = digitalRead(PIN_EMERGENCY);
  if (stopVal != lastStop) {
    sendCmd("SYS", "ALARM", stopVal == HIGH ? "ON" : "OFF");
    lastStop = stopVal;
  }

  // 3. SCHALTER & HEARTBEAT (Nur wenn ACTIVE)
  if (currentState == S_ACTIVE) {
    
    // Schalter scannen
    static int lastSw[8];
    for (int i = 0; i < 8; i++) {
      int val = digitalRead(PIN_SW[i]);
      if (val != lastSw[i]) {
        // 1 = AN (LOW), 0 = AUS (HIGH)
        sendCmd("DATA", String(i), val == LOW ? "1" : "0");
        lastSw[i] = val;
      }
    }

    // Heartbeat alle 2 Sekunden
    static unsigned long lastBeat = 0;
    if (now - lastBeat > 2000) {
      sendCmd("SYS", "LIFE", String(now));
      lastBeat = now;
    }
  }
}