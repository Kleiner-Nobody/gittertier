/**
 * GITTERTIER PRO - SENSOR HUB (Arduino UNO R3)
 * Liest 3-Wege-Schalter korrekt aus und sendet Zustandsänderungen via Serial an den R4
 * Inklusive Advanced Audio Engine (PAM8403)
 * Baudrate: 115200
 */

// =========================================================================
// 1. SOUND & AUDIO KONFIGURATION
// =========================================================================
const bool SOUND_AKTIV = true; // Setze auf 'false', um das System stummzuschalten

// Timing-Variablen für Non-Blocking Sounds
unsigned long letzterAlarmTon = 0;
bool alarmHiLo = false;

unsigned long letzterBlinkerTon = 0;
bool blinkerTickTack = false;

// =========================================================================
// 2. PIN DEFINITIONEN
// =========================================================================
// Schalter 1: Information Dome (3-Wege)
const int PIN_DOME_L       = 2;  
const int PIN_DOME_R       = 3;  

// Schalter 2: Blinker (3-Wege)
const int PIN_BLINKER_L    = 4;  
const int PIN_BLINKER_R    = 5;  

// Schalter 3: Geschwindigkeit (3-Wege)
const int PIN_SPEED_FAST   = 8;  
const int PIN_SPEED_SLOW   = 9;  

// Schalter 4: Navigation (3-Wege)
const int PIN_NAVI_AUTO    = 6;  
const int PIN_NAVI_ON      = 7;  

// Schalter 5: Autonom (2-Wege)
const int PIN_AUTONOM      = A0; 

// System-Pins
const int PIN_POWER_TASTER = 10; 
const int PIN_LAUT_R       = 11; // Rechter Kanal PAM8403
const int PIN_LAUT_L       = 12; // Linker Kanal PAM8403
const int PIN_NOT_AUS      = 13; 

// =========================================================================
// 3. LOGIK VARIABLEN
// =========================================================================
int lastDome = -1, lastBlinker = -1, lastSpeed = -1, lastNavi = -1, lastAutonom = -1;

bool systemAktiv = false;
int letzterPowerStatus = HIGH;
unsigned long letzteEntprellZeit = 0;
const int entprellVerzoegerung = 50;
bool letzterNotAusStatus = HIGH;

// =========================================================================
// 4. AUDIO ENGINE (Sounds)
// =========================================================================

void playStartupSound() {
  if (!SOUND_AKTIV) return;
  // Apple-like Arpeggio (Aufsteigender C-Dur Akkord)
  int notes[] = {262, 330, 392, 523}; // C4, E4, G4, C5
  for (int i = 0; i < 4; i++) {
    tone(PIN_LAUT_R, notes[i], 80);
    delay(100);
  }
  tone(PIN_LAUT_R, 1047, 400); // Heller Abschluss-Ping (C6)
  delay(400);
}

void playShutdownSound() {
  if (!SOUND_AKTIV) return;
  // Absteigender Akkord
  int notes[] = {523, 392, 330, 262}; 
  for (int i = 0; i < 4; i++) {
    tone(PIN_LAUT_L, notes[i], 80);
    delay(100);
  }
  tone(PIN_LAUT_L, 131, 400); // Tiefer Abschluss-Ton (C3)
  delay(400);
}

void handleAlarmSound() {
  if (!SOUND_AKTIV) return;
  // Sirene: Wechselt alle 500ms zwischen Hi und Lo
  if (millis() - letzterAlarmTon > 500) {
    letzterAlarmTon = millis();
    alarmHiLo = !alarmHiLo;
    
    if (alarmHiLo) tone(PIN_LAUT_R, 1200); // Hi
    else tone(PIN_LAUT_R, 800);            // Lo
  }
}

void handleBlinkerSound(int richtung) {
  if (!SOUND_AKTIV || richtung == 0) return;
  
  // Relais-Klicken: Synchronsiert auf 400ms (wie R4 Visuals)
  if (millis() - letzterBlinkerTon > 400) {
    letzterBlinkerTon = millis();
    blinkerTickTack = !blinkerTickTack;
    
    // Ton kommt aus dem passenden Lautsprecher!
    int aktiverPin = (richtung == 1) ? PIN_LAUT_L : PIN_LAUT_R;
    
    if (blinkerTickTack) {
      tone(aktiverPin, 1500, 15); // Kurzes helles "Tick"
    } else {
      tone(aktiverPin, 1000, 15); // Kurzes tieferes "Tack"
    }
  }
}

void stopAlleToene() {
  noTone(PIN_LAUT_L);
  noTone(PIN_LAUT_R);
}

// =========================================================================
// 5. SETUP
// =========================================================================
void setup() {
  Serial.begin(115200); 
  
  pinMode(PIN_DOME_L, INPUT_PULLUP);
  pinMode(PIN_DOME_R, INPUT_PULLUP);
  pinMode(PIN_BLINKER_L, INPUT_PULLUP);
  pinMode(PIN_BLINKER_R, INPUT_PULLUP);
  pinMode(PIN_SPEED_FAST, INPUT_PULLUP);
  pinMode(PIN_SPEED_SLOW, INPUT_PULLUP);
  pinMode(PIN_NAVI_AUTO, INPUT_PULLUP);
  pinMode(PIN_NAVI_ON, INPUT_PULLUP);
  pinMode(PIN_AUTONOM, INPUT_PULLUP); 
  
  pinMode(PIN_POWER_TASTER, INPUT_PULLUP);
  pinMode(PIN_NOT_AUS, INPUT_PULLUP);
  
  pinMode(PIN_LAUT_L, OUTPUT);
  pinMode(PIN_LAUT_R, OUTPUT);

  // Kurzer Bestätigungston für Hardware-Boot
  if (SOUND_AKTIV) {
    tone(PIN_LAUT_R, 2000, 50);
  }
}

// =========================================================================
// 6. MAIN LOOP
// =========================================================================
void loop() {
  // --- NOT-AUS CHECK ---
  bool aktuellerNotAus = digitalRead(PIN_NOT_AUS);
  if (aktuellerNotAus != letzterNotAusStatus) {
    if (aktuellerNotAus == HIGH) {
      Serial.println("[SYS:ALARM:ON]");
      systemStopp();
    } else {
      Serial.println("[SYS:ALARM:OFF]");
      stopAlleToene(); // Beendet die Sirene
    }
    letzterNotAusStatus = aktuellerNotAus;
    delay(50);
  }

  // Bei Not-Aus Sirene abspielen und restliche Logik blockieren
  if (aktuellerNotAus == HIGH) {
    handleAlarmSound();
    return; 
  }

  // --- POWER-TOGGLE ---
  int aktuellerPowerStatus = digitalRead(PIN_POWER_TASTER);
  if (aktuellerPowerStatus == LOW && letzterPowerStatus == HIGH && (millis() - letzteEntprellZeit) > entprellVerzoegerung) {
    systemAktiv = !systemAktiv;
    letzteEntprellZeit = millis();
    
    if (systemAktiv) {
      Serial.println("[SYS:POWER:ON]");
      playStartupSound();
      sendeAlleZustaende(); 
    } else {
      Serial.println("[SYS:POWER:OFF]");
      playShutdownSound();
      stopAlleToene(); // Zur Sicherheit alles muten
    }
  }
  letzterPowerStatus = aktuellerPowerStatus;
  
  if (!systemAktiv) return; // Wenn System aus, keine Schalter auswerten

  // --- SCHALTER ABFRAGEN & SOUNDS UPDATEN ---
  checkAlleSchalter();
  
  // Durchgehenden Blinker-Sound abspielen, falls aktiviert
  if (lastBlinker == 1 || lastBlinker == 2) {
    handleBlinkerSound(lastBlinker);
  }
}

// =========================================================================
// 7. HELPER FUNCTIONS
// =========================================================================
int read3Pos(int pinL, int pinR) {
  if (digitalRead(pinL) == LOW) return 1;       
  if (digitalRead(pinR) == LOW) return 2;       
  return 0;                                     
}

void checkAlleSchalter() {
  // DOME (Ist in V18.8 auf den Blinker gemappt!)
  int d = read3Pos(PIN_DOME_L, PIN_DOME_R);
  if(d != lastDome) { 
    Serial.println("[SW:DOME:" + String(d) + "]"); 
    lastDome = d; delay(20); 
  }

  // BLINKER (Ist in V18.8 auf Dome-Effekte gemappt!)
  int b = read3Pos(PIN_BLINKER_L, PIN_BLINKER_R);
  if(b != lastBlinker) { 
    Serial.println("[SW:BLINK:" + String(b) + "]"); 
    // Blinker-Audio-Reset, damit der erste Tick sofort kommt
    letzterBlinkerTon = 0; 
    blinkerTickTack = false;
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
  systemAktiv = false; 
}