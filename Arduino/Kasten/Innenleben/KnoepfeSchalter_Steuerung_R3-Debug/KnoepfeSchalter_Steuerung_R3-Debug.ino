/**
 * Gittertier Pro - Vollständige Debug-Steuerung
 * Baudrate: 115200
 */

// --- PIN DEFINITIONEN (Exakt nach deiner Tabelle) ---
const int PIN_OBEN_RECHTS_L  = 2;  // Schwarz
const int PIN_OBEN_RECHTS_R  = 3;  // Braun
const int PIN_OBEN_LINKS_L   = 4;  // Gelb
const int PIN_OBEN_LINKS_R   = 5;  // Grün
const int PIN_UNTEN_RECHTS_L = 6;  // Rot
const int PIN_UNTEN_RECHTS_R = 7;  // Weiß
const int PIN_UNTEN_LINKS_L  = 8;  // Blau
const int PIN_UNTEN_LINKS_R  = 9;  // Lila

const int PIN_POWER_TASTER    = 10; // Blau (Momentary)
const int PIN_LAUTSPRECHER_R  = 11; // Lila
const int PIN_LAUTSPRECHER_L  = 12; // Gelb
const int PIN_NOT_AUS         = 13; // Rot (Öffner / NC)

// --- LOGIK VARIABLEN ---
bool systemAktiv = false;
int letzterPowerStatus = HIGH;
unsigned long letzteEntprellZeit = 0;
const int entprellVerzoegerung = 50;

void setup() {
  Serial.begin(115200);
  
  // Alle Schalter-Pins als Input mit Pullup initialisieren
  for (int i = 2; i <= 10; i++) {
    pinMode(i, INPUT_PULLUP);
  }
  pinMode(PIN_NOT_AUS, INPUT_PULLUP);
  
  pinMode(PIN_LAUTSPRECHER_L, OUTPUT);
  pinMode(PIN_LAUTSPRECHER_R, OUTPUT);

  Serial.println(F("========================================"));
  Serial.println(F("   GITTERTIER PRO - DEBUG MODUS AKTIV   "));
  Serial.println(F("========================================"));
  Serial.println(F("Warte auf Power-Knopf (Pin 10)..."));
}

void loop() {
  // 1. NOT-AUS CHECK (NC Logik)
  if (digitalRead(PIN_NOT_AUS) == HIGH) {
    systemStopp();
    return; 
  }

  // 2. POWER-TOGGLE (Pin 10: 1 -> 0 -> 1 Erkennung)
  int aktuellerPowerStatus = digitalRead(PIN_POWER_TASTER);
  if (aktuellerPowerStatus == LOW && letzterPowerStatus == HIGH && (millis() - letzteEntprellZeit) > entprellVerzoegerung) {
    systemAktiv = !systemAktiv;
    letzteEntprellZeit = millis();
    Serial.print(F(">>> SYSTEM POWER: "));
    Serial.println(systemAktiv ? F("AN") : F("AUS"));
    
    if (systemAktiv) {
      // Anschaltton (kurze Frequenzleiter)
      tone(PIN_LAUTSPRECHER_L, 440, 100); delay(100);
      tone(PIN_LAUTSPRECHER_L, 880, 200);
    }
  }
  letzterPowerStatus = aktuellerPowerStatus;

  // Wenn aus, dann hier abbrechen
  if (!systemAktiv) return;

  // 3. ALLE DREHSCHALTER ABFRAGEN
  checkAlleSchalter();
  
  delay(100); // Kleine Pause für die Lesbarkeit im Serial Monitor
}

void checkAlleSchalter() {
  // Wir prüfen jeden Pin und geben nur bei "LOW" (aktiviert) eine Meldung aus
  
  // Oben Rechts (Geschwindigkeit/Turbo laut Planung)
  if (digitalRead(PIN_OBEN_RECHTS_L) == LOW) Serial.println(F("Drehschalter Oben Rechts: LINKS"));
  if (digitalRead(PIN_OBEN_RECHTS_R) == LOW) Serial.println(F("Drehschalter Oben Rechts: RECHTS (Turbo?)"));

  // Oben Links (Blinker laut Planung)
  if (digitalRead(PIN_OBEN_LINKS_L) == LOW) {
    Serial.println(F("Drehschalter Oben Links: LINKS (Blinker L)"));
    tone(PIN_LAUTSPRECHER_L, 600, 50); 
  }
  if (digitalRead(PIN_OBEN_LINKS_R) == LOW) {
    Serial.println(F("Drehschalter Oben Links: RECHTS (Blinker R)"));
    tone(PIN_LAUTSPRECHER_R, 600, 50);
  }

  // Unten Rechts (Disco/Licht laut Planung)
  if (digitalRead(PIN_UNTEN_RECHTS_L) == LOW) Serial.println(F("Drehschalter Unten Rechts: LINKS (Disco Modus)"));
  if (digitalRead(PIN_UNTEN_RECHTS_R) == LOW) Serial.println(F("Drehschalter Unten Rechts: RECHTS"));

  // Unten Links (Autonom/Navi laut Planung)
  if (digitalRead(PIN_UNTEN_LINKS_L) == LOW) Serial.println(F("Drehschalter Unten Links: LINKS (Autonom)"));
  if (digitalRead(PIN_UNTEN_LINKS_R) == LOW) Serial.println(F("Drehschalter Unten Links: RECHTS (Navi)"));
}

void systemStopp() {
  noTone(PIN_LAUTSPRECHER_L);
  noTone(PIN_LAUTSPRECHER_R);
  // Hier könnten Befehle an den Arduino R4 gesendet werden, um Motoren zu stoppen
}