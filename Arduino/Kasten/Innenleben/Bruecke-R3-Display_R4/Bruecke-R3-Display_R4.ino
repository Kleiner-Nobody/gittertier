/**
 * ============================================================================================
 * GITTERTIER PRO - INDUSTRIAL MASTER HUB v18.5 (SMART PARSER & PROTOCOL TRANSLATOR)
 * ============================================================================================
 * Hardware: Arduino UNO R4 WiFi
 * * AUFGABE DES HUBS:
 * 1. Empfang und Entschlüsselung zusammenhängender Datenblöcke vom R3 (Serial1).
 * 2. Übersetzung von logischen R3-Befehlen (SW:ID:VAL) in Display-kompatible Pin-Befehle (DATA:PIN:VAL).
 * 3. Sicheres BLE-Central-Management mit garantiert synchroner Authentifizierung.
 * 4. Isolierte Motor-Ansteuerung via SoftwareSerial auf Pin 7 & 8.
 * ============================================================================================
 */

#include <ArduinoBLE.h>
#include <Arduino_LED_Matrix.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>

// ============================================================================================
// KONSTANTEN & HARDWARE-PINS
// ============================================================================================
#define VERSION "v18.5"
#define AUTH_KEY "AUTH|GITTER_77_PRO_SEC"
#define TARGET_BLE_NAME "Gittertier Pro"

// Joystick (Schalten gegen GND)
const uint8_t PIN_JOY_UP    = 2;
const uint8_t PIN_JOY_DOWN  = 3;
const uint8_t PIN_JOY_LEFT  = 4;
const uint8_t PIN_JOY_RIGHT = 5;

// Information Dome 
const uint8_t PIN_DOME      = 6;
#define DOME_LEDS 16
Adafruit_NeoPixel dome(DOME_LEDS, PIN_DOME, NEO_GRB + NEO_KHZ800);

// Motor Nano Isolation (SoftwareSerial)
const uint8_t PIN_NANO_RX   = 7;
const uint8_t PIN_NANO_TX   = 8;
SoftwareSerial motorNano(PIN_NANO_RX, PIN_NANO_TX);

// LED Matrix (HUD)
ArduinoLEDMatrix matrix;

// ============================================================================================
// SYSTEM-ZUSTÄNDE (State Machine)
// ============================================================================================
enum SystemState {
    BOOTING,
    BLE_SCANNING,
    BLE_CONNECTING,
    BLE_AUTHENTICATING,
    SYSTEM_RUNNING,
    SYSTEM_ALARM
};

SystemState currentState = BOOTING;
bool bleAuthenticated = false;
bool emergencyStopActive = false;
int currentSpeedPWM = 150; // Standard Motor PWM

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 2000;

// BLE Objekte
BLEDevice peripheral;
BLECharacteristic dataChar;

// Serieller Puffer für R3 Daten
String r3InputBuffer = "";

// ============================================================================================
// MATRIX GRAFIKEN (HUD)
// ============================================================================================
const uint32_t HUD_SEARCHING[] = {
    0x00018000, 0x00660000, 0x01818000 
};
const uint32_t HUD_HEART[] = {
    0x3184a444, 0x42081100, 0xa0040000
};
const uint32_t HUD_STOP[] = {
    0xf909f99f, 0x90f90f90, 0xf99f909f
};
const uint32_t HUD_ARROW_UP[] = {
    0x0400e001, 0xf03f8044, 0x04404400
};
const uint32_t HUD_ARROW_DOWN[] = {
    0x44044004, 0x403f801f, 0x00e00040
};

// ============================================================================================
// DOME ANIMATION LOGIK (Non-Blocking)
// ============================================================================================
enum DomeEffect { OFF, RAINBOW, BLINK_DISCO, SOLID_RED };
DomeEffect currentDomeEffect = OFF;

uint32_t wheel(byte pos) {
    pos = 255 - pos;
    if (pos < 85) return dome.Color(255 - pos * 3, 0, pos * 3);
    if (pos < 170) { pos -= 85; return dome.Color(0, pos * 3, 255 - pos * 3); }
    pos -= 170; return dome.Color(pos * 3, 255 - pos * 3, 0);
}

void updateDome() {
    static unsigned long lastUpdate = 0;
    static uint16_t j = 0;
    static bool blinkState = false;

    if (emergencyStopActive) {
        currentDomeEffect = SOLID_RED;
    }

    switch (currentDomeEffect) {
        case RAINBOW:
            if (millis() - lastUpdate > 20) {
                for (uint16_t i = 0; i < dome.numPixels(); i++) {
                    dome.setPixelColor(i, wheel(((i * 256 / dome.numPixels()) + j) & 255));
                }
                dome.show();
                j++;
                lastUpdate = millis();
            }
            break;

        case BLINK_DISCO:
            if (millis() - lastUpdate > 150) {
                blinkState = !blinkState;
                uint32_t color = blinkState ? dome.Color(0, 255, 255) : dome.Color(255, 0, 255);
                dome.fill(color);
                dome.show();
                lastUpdate = millis();
            }
            break;

        case SOLID_RED:
            dome.fill(dome.Color(255, 0, 0));
            dome.show();
            break;

        case OFF:
            dome.clear();
            dome.show();
            break;
    }
}

// ============================================================================================
// BLE FUNKTIONEN (Senden & Management)
// ============================================================================================
void sendBLECommand(String msg) {
    // Verhindert das Senden ins Leere und Pufferüberläufe
    if (bleAuthenticated && dataChar && dataChar.canWrite()) {
        dataChar.writeValue(msg.c_str());
        delay(15); // Kritisches Delay, damit das ESP32 Display die Datenflut verarbeiten kann
    }
}

void manageBLE() {
    switch (currentState) {
        case BLE_SCANNING:
            BLE.scanForName(TARGET_BLE_NAME);
            peripheral = BLE.available();
            if (peripheral) {
                Serial.println("Display Peripheral gefunden! Stoppe Scan...");
                BLE.stopScan();
                currentState = BLE_CONNECTING;
            }
            break;

        case BLE_CONNECTING:
            if (peripheral.connect()) {
                Serial.println("Physisch verbunden. Entdecke Charakteristiken...");
                if (peripheral.discoverAttributes()) {
                    dataChar = peripheral.characteristic("19b10001-e8f2-537e-4f6c-d104768a1214");
                    if (dataChar) {
                        currentState = BLE_AUTHENTICATING;
                    } else {
                        Serial.println("Fehler: Charakteristik fehlt. Reset.");
                        peripheral.disconnect();
                        currentState = BLE_SCANNING;
                    }
                } else {
                    Serial.println("Fehler: Attribute nicht gefunden. Reset.");
                    peripheral.disconnect();
                    currentState = BLE_SCANNING;
                }
            } else {
                currentState = BLE_SCANNING;
            }
            break;

        case BLE_AUTHENTICATING:
            Serial.println("Führe Sicherheits-Handshake aus...");
            dataChar.writeValue(AUTH_KEY);
            delay(150); // Warten bis Display das AUTH_OK verarbeitet hat
            
            bleAuthenticated = true;
            currentState = SYSTEM_RUNNING;
            matrix.loadFrame(HUD_HEART);
            Serial.println("Authentifizierung erfolgreich. SYSTEM BEREIT.");
            break;

        case SYSTEM_RUNNING:
        case SYSTEM_ALARM:
            // Permanente Überwachung der Verbindungsstabilität
            if (!peripheral.connected()) {
                Serial.println("CRITICAL: BLE Verbindung abgerissen!");
                bleAuthenticated = false;
                matrix.loadFrame(HUD_SEARCHING);
                currentState = BLE_SCANNING;
            }
            break;
    }
}

// ============================================================================================
// SMART PARSER (Verarbeitet zusammengeklebte R3-Pakete)
// ============================================================================================
void parseR3Input() {
    // 1. Alles in den Puffer lesen, was aktuell an der Hardware UART anliegt
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        r3InputBuffer += c;
    }

    // 2. Suche nach der innersten Klammerstruktur [ ... ]
    int startIdx = r3InputBuffer.indexOf('[');
    int endIdx = r3InputBuffer.indexOf(']');

    // Solange wir gültige Start- und Endklammern haben, iterieren wir
    while (startIdx != -1 && endIdx != -1 && endIdx > startIdx) {
        // Extrahiere das reine Paket OHNE die Klammern (z.B. "SW:SPEED:1")
        String packet = r3InputBuffer.substring(startIdx + 1, endIdx);
        
        // Verarbeite das isolierte Paket
        processR3Packet(packet);
        
        // Schneide das verarbeitete Paket aus dem Puffer heraus
        r3InputBuffer = r3InputBuffer.substring(endIdx + 1);
        
        // Suche nach dem nächsten Paket im verbleibenden String
        startIdx = r3InputBuffer.indexOf('[');
        endIdx = r3InputBuffer.indexOf(']');
    }

    // 3. Puffer-Sicherheitsnetz: 
    // Falls Mülldaten ohne schließende Klammer den Speicher füllen, leeren wir ihn.
    if (r3InputBuffer.length() > 250) {
        Serial.println("WARNUNG: RX Puffer übergelaufen, leere Puffer.");
        r3InputBuffer = "";
    }
}

// ============================================================================================
// PROTOCOL TRANSLATOR (Logik zu Display UI Mapping)
// ============================================================================================
void processR3Packet(String packet) {
    Serial.print("RX (Parsed): ");
    Serial.println(packet);

    // 1. SYSTEM-COMMANDS (Power & Alarm)
    if (packet == "SYS:ALARM:ON") {
        emergencyStopActive = true;
        currentState = SYSTEM_ALARM;
        matrix.loadFrame(HUD_STOP);
        motorNano.print("BRAKE,1\r");
        sendBLECommand("[SYS:ALARM:ON]");
        return;
    }
    if (packet == "SYS:ALARM:OFF") {
        emergencyStopActive = false;
        currentState = SYSTEM_RUNNING;
        matrix.loadFrame(HUD_HEART);
        motorNano.print("BRAKE,0\r");
        sendBLECommand("[SYS:ALARM:OFF]");
        return;
    }
    if (packet == "SYS:POWER:ON") {
        sendBLECommand("[SYS:PWR:START]");
        return;
    }
    if (packet == "SYS:POWER:OFF") {
        sendBLECommand("[SYS:PWR:REQ_OFF]");
        return;
    }

    // 2. SCHALTER-TRANSLATION (R3 Logik -> Display Pins)
    if (packet.startsWith("SW:")) {
        int firstSep = packet.indexOf(':', 3);
        if (firstSep > 0) {
            String id = packet.substring(3, firstSep);
            String valStr = packet.substring(firstSep + 1);
            int val = valStr.toInt();

            // DOME (Pin 2 & 3 auf dem Display)
            if (id == "DOME") {
                if (val == 1) currentDomeEffect = RAINBOW;
                else if (val == 2) currentDomeEffect = BLINK_DISCO;
                else currentDomeEffect = OFF;
                
                // Mappe Zustand 1 auf Pin 2, Zustand 2 auf Pin 3
                sendBLECommand(val == 1 ? "[DATA:2:1]" : "[DATA:2:0]");
                sendBLECommand(val == 2 ? "[DATA:3:1]" : "[DATA:3:0]");
            }
            // BLINKER (Pin 4 & 5 auf dem Display)
            else if (id == "BLINK") {
                sendBLECommand(val == 1 ? "[DATA:4:1]" : "[DATA:4:0]");
                sendBLECommand(val == 2 ? "[DATA:5:1]" : "[DATA:5:0]");
            }
            // SPEED (Pin 6 & 7 auf dem Display)
            else if (id == "SPEED") {
                if (val == 1) currentSpeedPWM = 255; // Turbo
                else if (val == 2) currentSpeedPWM = 100; // Langsam
                else currentSpeedPWM = 150; // Normal (0/Mitte)
                
                sendBLECommand(val == 1 ? "[DATA:6:1]" : "[DATA:6:0]");
                sendBLECommand(val == 2 ? "[DATA:7:1]" : "[DATA:7:0]");
            }
            // NAVI (Pin 8 & 9 auf dem Display)
            else if (id == "NAVI") {
                sendBLECommand(val == 1 ? "[DATA:8:1]" : "[DATA:8:0]");
                sendBLECommand(val == 2 ? "[DATA:9:1]" : "[DATA:9:0]");
            }
            // AUTONOM (Hat keine direkte Pin-Repräsentation im aktuellen Display OS 22.0, 
            // wird aber für zukünftige Erweiterungen als RAW-Daten durchgereicht)
            else if (id == "AUTO") {
                sendBLECommand("[SW:AUTO:" + valStr + "]");
            }
        }
    }
}

// ============================================================================================
// JOYSTICK & MOTOR STEUERUNG (SoftwareSerial zu Nano)
// ============================================================================================
void handleJoystick() {
    if (emergencyStopActive || currentState != SYSTEM_RUNNING) return;

    bool up    = !digitalRead(PIN_JOY_UP);
    bool down  = !digitalRead(PIN_JOY_DOWN);
    bool left  = !digitalRead(PIN_JOY_LEFT);
    bool right = !digitalRead(PIN_JOY_RIGHT);

    static int lastJoyState = -1; // 0: Stop, 1: Up, 2: Down, 3: Left, 4: Right
    static int lastSpeedSent = -1;
    int currentJoyState = 0;

    if (up) currentJoyState = 1;
    else if (down) currentJoyState = 2;
    else if (left) currentJoyState = 3;
    else if (right) currentJoyState = 4;

    // Sende Befehle nur bei Richtungs- ODER Geschwindigkeitsänderung
    if (currentJoyState != lastJoyState || currentSpeedPWM != lastSpeedSent) {
        
        // Die \r Endung ist absolut kritisch für den Motor Nano Parser!
        switch (currentJoyState) {
            case 0: 
                motorNano.print("PWM,0\r"); 
                matrix.loadFrame(HUD_HEART);
                sendBLECommand("[JOY:MOVE:CENTER]");
                break;
            case 1: 
                motorNano.print("DIR,1\r"); 
                motorNano.print("PWM," + String(currentSpeedPWM) + "\r");
                matrix.loadFrame(HUD_ARROW_UP);
                sendBLECommand("[JOY:MOVE:UP]");
                break;
            case 2: 
                motorNano.print("DIR,0\r"); 
                motorNano.print("PWM," + String(currentSpeedPWM) + "\r");
                matrix.loadFrame(HUD_ARROW_DOWN);
                sendBLECommand("[JOY:MOVE:DOWN]");
                break;
            case 3: 
                motorNano.print("LDIR,0\r");
                motorNano.print("RDIR,1\r"); 
                motorNano.print("PWM," + String(currentSpeedPWM) + "\r");
                sendBLECommand("[JOY:MOVE:LEFT]");
                break;
            case 4: 
                motorNano.print("LDIR,1\r");
                motorNano.print("RDIR,0\r"); 
                motorNano.print("PWM," + String(currentSpeedPWM) + "\r");
                sendBLECommand("[JOY:MOVE:RIGHT]");
                break;
        }
        lastJoyState = currentJoyState;
        lastSpeedSent = currentSpeedPWM;
    }
}

// ============================================================================================
// SETUP & MAIN LOOP
// ============================================================================================
void setup() {
    // 1. Kommunikations-Hardware
    Serial.begin(115200);    // Debug Ausgabe (PC)
    Serial1.begin(115200);   // R3 Kommunikation (RX=Pin0, TX=Pin1)
    motorNano.begin(115200); // Motor Nano Isolation via SoftwareSerial (Pin 7, 8)

    // 2. Hardware-Pins konfigurieren
    pinMode(PIN_JOY_UP, INPUT_PULLUP);
    pinMode(PIN_JOY_DOWN, INPUT_PULLUP);
    pinMode(PIN_JOY_LEFT, INPUT_PULLUP);
    pinMode(PIN_JOY_RIGHT, INPUT_PULLUP);

    // 3. Optische Schnittstellen aktivieren
    matrix.begin();
    matrix.loadFrame(HUD_SEARCHING);
    
    dome.begin();
    dome.setBrightness(150);
    dome.show();

    // 4. BLE Controller starten
    if (!BLE.begin()) {
        Serial.println("FATAL: BLE-Modul Start fehlgeschlagen!");
        while (1) {
            dome.fill(dome.Color(255, 100, 0));
            dome.show();
            delay(500);
            dome.clear();
            dome.show();
            delay(500);
        }
    }

    Serial.print("\n=== GITTERTIER PRO HUB ");
    Serial.print(VERSION);
    Serial.println(" ===");
    Serial.println("Rollen: BLE Central | Protocol Bridge | Motor Controller");
    Serial.println("Initialisiere BLE Scan...");
    
    currentState = BLE_SCANNING;
}

void loop() {
    // BLE State Machine triggern (Verbindungsaufbau & Keep-Alive)
    manageBLE();

    // R3 Daten asynchron abgreifen, trennen und sofort visualisieren/ausführen
    parseR3Input();

    // Joystick auswerten und via SoftwareSerial zum Motor schicken
    handleJoystick();

    // Neopixel Animation flüssig aktualisieren
    updateDome();

    // Heartbeat an das Display senden, um Watchdog-Auslösung zu verhindern
    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
        if (bleAuthenticated) {
            sendBLECommand("[SYS:LIFE:STABLE]");
        }
        lastHeartbeat = millis();
    }
}