/**
 * GITTERTIER PRO - INDUSTRIAL MASTER HUB (v15.0 - DOME EDITION)
 * Hardware: Arduino UNO R4 WiFi
 *
 * AUFGABE:
 * - Brücke zwischen UNO R3 (Serial1) und ESP32-S3 Display (BLE)
 * - Steuerung Information Dome (NeoPixel an D6)
 * - Auslesen des 5-Wege Joysticks (D2-D5)
 * - Steuerung der 12x8 LED-Matrix
 */

#include <ArduinoBLE.h>
#include <Arduino_LED_Matrix.h>
#include <Adafruit_NeoPixel.h> // Bibliothek für den Information Dome

// =========================================================================
// 1. HARDWARE-SETUP & PINS
// =========================================================================
const uint8_t PIN_JOY_UP    = 2;
const uint8_t PIN_JOY_DOWN  = 3;
const uint8_t PIN_JOY_LEFT  = 4;
const uint8_t PIN_JOY_RIGHT = 5;

// Information Dome Settings
#define DOME_PIN    6
#define DOME_LEDS   16       // Anzahl der LEDs im Ring/Dome
Adafruit_NeoPixel dome(DOME_LEDS, DOME_PIN, NEO_GRB + NEO_KHZ800);

ArduinoLEDMatrix matrix;

// =========================================================================
// 2. BLE & PROTOKOLL KONFIGURATION
// =========================================================================
const char* DISPLAY_NAME = "Gittertier Pro";
const char* SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214";
const char* CHAR_UUID    = "19b10001-e8f2-537e-4f6c-d104768a1214";
const char* AUTH_KEY     = "GITTER_77_PRO_SEC";

BLEService        gService(SERVICE_UUID);
BLEStringCharacteristic displayChar(CHAR_UUID, BLERead | BLEWrite | BLENotify, 30);

bool isAuthorized = false;

// =========================================================================
// 3. MATRIX SYMBOLS (Feedback)
// =========================================================================
const uint32_t SYMBOL_HEART[] = {
    0x3184a444,
    0x42081100,
    0xa0040000
};
const uint32_t SYMBOL_STOP[] = {
    0xf909f99f,
    0x90f90f90,
    0xf99f909f
};
const uint32_t SYMBOL_ERROR[] = {
    0xb4000005,
    0x500aa005,
    0x0000002d
};

// =========================================================================
// 4. DOME STATE MACHINE (Non-Blocking)
// =========================================================================
enum DomeMode { DOME_OFF, DOME_RAINBOW, DOME_BLINK };
DomeMode currentDomeMode = DOME_OFF;

unsigned long lastDomeUpdate = 0;
uint16_t domePixelCycle = 0;
uint16_t domeBlinkState = 0; // 0=Rot, 1=Grün, 2=Blau, 3=Weiß

// Helper: Wheel Funktion für Regenbogenfarben
uint32_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if(WheelPos < 85) {
        return dome.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    }
    if(WheelPos < 170) {
        WheelPos -= 85;
        return dome.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
    WheelPos -= 170;
    return dome.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void updateDome() {
    unsigned long now = millis();

    if (currentDomeMode == DOME_OFF) {
        // Sicherstellen, dass alles aus ist (einmalig)
        static bool isCleared = false;
        if (!isCleared) {
            dome.clear();
            dome.show();
            isCleared = true;
        }
        return;
    }

    // RAINBOW MODUS (Schalter Links)
    if (currentDomeMode == DOME_RAINBOW) {
        if (now - lastDomeUpdate > 20) { // Speed: 20ms
            for(int i=0; i< dome.numPixels(); i++) {
                dome.setPixelColor(i, Wheel(((i * 256 / dome.numPixels()) + domePixelCycle) & 255));
            }
            dome.show();
            domePixelCycle++;
            lastDomeUpdate = now;
        }
    }
    // BLINK MODUS (Schalter Rechts)
    else if (currentDomeMode == DOME_BLINK) {
        if (now - lastDomeUpdate > 1000) { // Speed: 1000ms (1 Sekunde)
            uint32_t c = 0;
            switch(domeBlinkState) {
                case 0: c = dome.Color(255, 0, 0); break;   // Rot
                case 1: c = dome.Color(0, 255, 0); break;   // Grün
                case 2: c = dome.Color(0, 0, 255); break;   // Blau
                case 3: c = dome.Color(255, 255, 255); break; // Weiß
            }
            dome.fill(c);
            dome.show();
            
            domeBlinkState++;
            if (domeBlinkState > 3) domeBlinkState = 0;
            lastDomeUpdate = now;
        }
    }
}

// =========================================================================
// 5. HELPER FUNCTIONS
// =========================================================================

void updateMatrix(const uint32_t icon[]) {
    matrix.loadFrame(icon);
}

void manageBLE() {
    BLEDevice central = BLE.central();
    if (central) {
        if (central.connected()) {
           // Connection active
        }
    }
}

void showBootProgress(int percent) {
    // Simpel: Zeige Prozent auf Serial, Matrix macht Animation
    Serial.print("Boot: "); Serial.print(percent); Serial.println("%");
}

// =========================================================================
// 6. SERIAL PARSER (R3 -> R4)
// =========================================================================

String inputBuffer = "";

void parseR3Data() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        
        if (c == '[') {
            inputBuffer = ""; // Start packet
        } else if (c == ']') {
            // Paket Ende -> Verarbeiten
            // Format: TYPE:ID:VAL  (z.B. SW:1:1)
            
            int firstSep = inputBuffer.indexOf(':');
            int secondSep = inputBuffer.indexOf(':', firstSep + 1);
            
            if (firstSep > 0 && secondSep > 0) {
                String type = inputBuffer.substring(0, firstSep);
                String id   = inputBuffer.substring(firstSep + 1, secondSep);
                String val  = inputBuffer.substring(secondSep + 1);
                
                // --- LOGIK FÜR INFORMATION DOME (Schalter oben Rechts) ---
                // Laut Kabelplan: 
                // Schalter Oben Rechts (Links)  = Pin 2 am R3 = ID "1" im Protokoll
                // Schalter Oben Rechts (Rechts) = Pin 3 am R3 = ID "2" im Protokoll
                
                if (type == "SW") {
                    if (id == "1") { // Links geschaltet
                        if (val == "1") {
                            currentDomeMode = DOME_RAINBOW;
                            Serial.println("Dome: Rainbow Active");
                        } else {
                            // Wenn ausgeschaltet, und wir im Rainbow Modus waren -> OFF
                            if (currentDomeMode == DOME_RAINBOW) currentDomeMode = DOME_OFF;
                        }
                    }
                    else if (id == "2") { // Rechts geschaltet
                        if (val == "1") {
                            currentDomeMode = DOME_BLINK;
                            domeBlinkState = 0; // Reset Blink sequence
                            Serial.println("Dome: Blink Active");
                        } else {
                            // Wenn ausgeschaltet, und wir im Blink Modus waren -> OFF
                            if (currentDomeMode == DOME_BLINK) currentDomeMode = DOME_OFF;
                        }
                    }
                }
                
                // Weiterleitung an Display via BLE
                // Wir senden ALLES weiter, damit das Display auch Bescheid weiß
                if (displayChar.written() || true) { // Check connection logic
                     String bleMsg = "[" + inputBuffer + "]";
                     displayChar.writeValue(bleMsg.c_str());
                }
                
                // NOT-AUS Logik für Matrix
                if (type == "SYS" && id == "ALARM" && val == "ON") {
                    updateMatrix(SYMBOL_STOP);
                    // Bei Not-Aus auch Dome rot machen?
                    dome.fill(dome.Color(255, 0, 0));
                    dome.show();
                    // Blockiert hier nicht den Loop, aber visualisiert
                }
            }
            inputBuffer = "";
        } else {
            inputBuffer += c;
        }
    }
}

// =========================================================================
// 7. SETUP & MAIN LOOP
// =========================================================================

void setup() {
    Serial.begin(115200);   // Debug zum PC
    Serial1.begin(115200);  // Datenleitung vom R3 (RX/TX Pins)
    
    // Matrix Init
    matrix.begin();
    updateMatrix(SYMBOL_HEART); // Start-Symbol
    
    // Dome Init
    dome.begin();
    dome.show();
    dome.setBrightness(100);
    
    // Joystick Pins
    pinMode(PIN_JOY_UP, INPUT_PULLUP);
    pinMode(PIN_JOY_DOWN, INPUT_PULLUP);
    pinMode(PIN_JOY_LEFT, INPUT_PULLUP);
    pinMode(PIN_JOY_RIGHT, INPUT_PULLUP);

    // BLE Init
    if (!BLE.begin()) {
        Serial.println(F("BLE-Modul konnte nicht gestartet werden!"));
        updateMatrix(SYMBOL_ERROR);
        while (1);
    }

    BLE.setLocalName(DISPLAY_NAME);
    BLE.setAdvertisedService(gService);
    gService.addCharacteristic(displayChar);
    BLE.addService(gService);
    BLE.advertise();
    
    Serial.println(F("R4 ONLINE. Suche Display..."));
}

void loop() {
    // 1. BLE Management
    BLE.poll(); // Wichtig für Event-Handling

    // 2. Kommunikation vom R3 abwickeln
    parseR3Data();
    
    // 3. Information Dome Animation (Non-Blocking)
    updateDome();
    
    // 4. Joystick Logik (optional für Senden an Display)
    // Hier könnte man Joystick Events an das Display senden
}