/**
 * GITTERTIER PRO - SENSOR HUB (Arduino UNO R3)
 * Liest 3-Wege-Schalter korrekt aus und sendet Zustandsänderungen via Serial an den R4
 * Baudrate: 115200
 */

// --- PIN DEFINITIONEN ---
// Schalter 1: Information Dome (3-Wege)
const int PIN_DOME_L       = 2;  // Rainbow
const int PIN_DOME_R       = 3;  // Blink/Disco

// Schalter 2: Blinker (3-Wege)
const int PIN_BLINKER_L    = 4;  // Links
const int PIN_BLINKER_R    = 5;  // Rechts

// Schalter 3: Geschwindigkeit (3-Wege)
const int PIN_SPEED_FAST   = 6;  // Schnell / Turbo
const int PIN_SPEED_SLOW   = 7;  // Langsam

// Schalter 4: Navigation (3-Wege)
const int PIN_NAVI_AUTO    = 8;  // Auto On
const int PIN_NAVI_ON      = 9;  // Navi

// Schalter 5: Autonom (2-Wege) -> DER FEHLENDE SW 8!
const int PIN_AUTONOM      = A0; // On / Off

// System-Pins
const int PIN_POWER_TASTER = 10; // Blau (Momentary)
const int PIN_LAUT_R       = 11; // Lila
const int PIN_LAUT_L       = 12; // Gelb
const int PIN_NOT_AUS      = 13; // Rot (Öffner / NC)

// --- LOGIK VARIABLEN ---
int lastDome = -1, lastBlinker = -1, lastSpeed = -1, lastNavi = -1, lastAutonom = -1;

bool systemAktiv = false;
int letzterPowerStatus = HIGH;
unsigned long letzteEntprellZeit = 0;
const int entprellVerzoegerung = 50;
bool letzterNotAusStatus = HIGH;

void setup() {
  Serial.begin(115200); // TX geht an RX vom R4
  
  // Alle Pins als Input mit Pullup initialisieren
  pinMode(PIN_DOME_L, INPUT_PULLUP);
  pinMode(PIN_DOME_R, INPUT_PULLUP);
  pinMode(PIN_BLINKER_L, INPUT_PULLUP);
  pinMode(PIN_BLINKER_R, INPUT_PULLUP);
  pinMode(PIN_SPEED_FAST, INPUT_PULLUP);
  pinMode(PIN_SPEED_SLOW, INPUT_PULLUP);
  pinMode(PIN_NAVI_AUTO, INPUT_PULLUP);
  pinMode(PIN_NAVI_ON, INPUT_PULLUP);
  pinMode(PIN_AUTONOM, INPUT_PULLUP); // SW 8
  
  pinMode(PIN_POWER_TASTER, INPUT_PULLUP);
  pinMode(PIN_NOT_AUS, INPUT_PULLUP);
  
  pinMode(PIN_LAUT_L, OUTPUT);
  pinMode(PIN_LAUT_R, OUTPUT);

  tone(PIN_LAUT_R, 1000, 100);
}

void loop() {
  // 1. NOT-AUS CHECK (NC Logik)
  bool aktuellerNotAus = digitalRead(PIN_NOT_AUS);
  if (aktuellerNotAus != letzterNotAusStatus) {
    if (aktuellerNotAus == HIGH) {
      Serial.println("[SYS:ALARM:ON]");
      systemStopp();
    } else {
      Serial.println("[SYS:ALARM:OFF]");
    }
    letzterNotAusStatus = aktuellerNotAus;
    delay(50);
  }

  if (aktuellerNotAus == HIGH) return; // Bei Not-Aus alles blockieren

  // 2. POWER-TOGGLE
  int aktuellerPowerStatus = digitalRead(PIN_POWER_TASTER);
  if (aktuellerPowerStatus == LOW && letzterPowerStatus == HIGH && (millis() - letzteEntprellZeit) > entprellVerzoegerung) {
    systemAktiv = !systemAktiv;
    letzteEntprellZeit = millis();
    
    if (systemAktiv) {
      Serial.println("[SYS:POWER:ON]");
      tone(PIN_LAUT_L, 440, 100); delay(100); tone(PIN_LAUT_L, 880, 200);
      sendeAlleZustaende(); // Status aller Schalter initial senden
    } else {
      Serial.println("[SYS:POWER:OFF]");
      tone(PIN_LAUT_R, 880, 100); delay(100); tone(PIN_LAUT_R, 440, 200);
    }
  }
  letzterPowerStatus = aktuellerPowerStatus;
  
  if (!systemAktiv) return; // Wenn System aus, keine Schalter auswerten

  // 3. SCHALTER ABFRAGEN
  checkAlleSchalter();
}

// Hilfsfunktion: Wandelt 2 Pins eines 3-Wege-Schalters in einen Zustand um
int read3Pos(int pinL, int pinR) {
  if (digitalRead(pinL) == LOW) return 1;       // Position 1
  if (digitalRead(pinR) == LOW) return 2;       // Position 2
  return 0;                                     // Mitte (Beide HIGH)
}

void checkAlleSchalter() {
  // DOME
  int d = read3Pos(PIN_DOME_L, PIN_DOME_R);
  if(d != lastDome) { 
    Serial.println("[SW:DOME:" + String(d) + "]"); 
    lastDome = d; delay(20); 
  }

  // BLINKER
  int b = read3Pos(PIN_BLINKER_L, PIN_BLINKER_R);
  if(b != lastBlinker) { 
    Serial.println("[SW:BLINK:" + String(b) + "]"); 
    if(b == 1) tone(PIN_LAUT_L, 600, 50);
    if(b == 2) tone(PIN_LAUT_R, 600, 50);
    lastBlinker = b; delay(20); 
  }

  // SPEED
  int s = read3Pos(PIN_SPEED_FAST, PIN_SPEED_SLOW);
  if(s != lastSpeed) { 
    Serial.println("[SW:SPEED:" + String(s) + "]"); 
    lastSpeed = s; delay(20); 
  }

  // NAVI
  int n = read3Pos(PIN_NAVI_AUTO, PIN_NAVI_ON);
  if(n != lastNavi) { 
    Serial.println("[SW:NAVI:" + String(n) + "]"); 
    lastNavi = n; delay(20); 
  }

  // AUTONOM (2-Wege Schalter)
  int a = (digitalRead(PIN_AUTONOM) == LOW) ? 1 : 0;
  if(a != lastAutonom) { 
    Serial.println("[SW:AUTO:" + String(a) + "]"); 
    lastAutonom = a; delay(20); 
  }
}

void sendeAlleZustaende() {
  lastDome = read3Pos(PIN_DOME_L, PIN_DOME_R);
  Serial.println("[SW:DOME:" + String(lastDome) + "]"); delay(10);
  
  lastBlinker = read3Pos(PIN_BLINKER_L, PIN_BLINKER_R);
  Serial.println("[SW:BLINK:" + String(lastBlinker) + "]"); delay(10);
  
  lastSpeed = read3Pos(PIN_SPEED_FAST, PIN_SPEED_SLOW);
  Serial.println("[SW:SPEED:" + String(lastSpeed) + "]"); delay(10);
  
  lastNavi = read3Pos(PIN_NAVI_AUTO, PIN_NAVI_ON);
  Serial.println("[SW:NAVI:" + String(lastNavi) + "]"); delay(10);
  
  lastAutonom = (digitalRead(PIN_AUTONOM) == LOW) ? 1 : 0;
  Serial.println("[SW:AUTO:" + String(lastAutonom) + "]"); delay(10);
}

void systemStopp() {
  noTone(PIN_LAUT_L);
  noTone(PIN_LAUT_R);
  systemAktiv = false; 
}