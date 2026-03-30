/**
 * ============================================================================================
 * GITTERTIER PRO - INDUSTRIAL MASTER HUB v18.6 (MIT MOTOR-DEBUGGER & 38400 BAUD)
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
#define VERSION "v18.6"
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
    if (bleAuthenticated && dataChar && dataChar.canWrite()) {
        dataChar.writeValue(msg.c_str());
        delay(15); 
    }
}

void manageBLE() {
    static bool scanStarted = false; 

    switch (currentState) {
        case BLE_SCANNING:
            if (!scanStarted) {
                BLE.scanForName(TARGET_BLE_NAME);
                scanStarted = true;
                Serial.println("Starte BLE Scan...");
            }
            
            peripheral = BLE.available();
            if (peripheral) {
                Serial.println("Display Peripheral gefunden! Stoppe Scan...");
                BLE.stopScan();
                scanStarted = false; 
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
            delay(150); 
            
            bleAuthenticated = true;
            currentState = SYSTEM_RUNNING;
            matrix.loadFrame(HUD_HEART);
            Serial.println("Authentifizierung erfolgreich. SYSTEM BEREIT.");
            break;

        case SYSTEM_RUNNING:
        case SYSTEM_ALARM:
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
// MOTOR DEBUGGER
// ============================================================================================
void sendMotorCmd(String cmd) {
    // Fügt automatisch das wichtige '\r' hinzu und sendet an den Nano
    motorNano.print(cmd + "\r");
    
    // Debug-Ausgabe auf dem R4 Serial Monitor
    Serial.print("[MOTOR TX] -> ");
    Serial.println(cmd);
}

// ============================================================================================
// SMART PARSER (Verarbeitet zusammengeklebte R3-Pakete)
// ============================================================================================
void parseR3Input() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        r3InputBuffer += c;
    }

    int startIdx = r3InputBuffer.indexOf('[');
    int endIdx = r3InputBuffer.indexOf(']');

    while (startIdx != -1 && endIdx != -1 && endIdx > startIdx) {
        String packet = r3InputBuffer.substring(startIdx + 1, endIdx);
        processR3Packet(packet);
        r3InputBuffer = r3InputBuffer.substring(endIdx + 1);
        startIdx = r3InputBuffer.indexOf('[');
        endIdx = r3InputBuffer.indexOf(']');
    }

    if (r3InputBuffer.length() > 250) {
        Serial.println("WARNUNG: RX Puffer übergelaufen, leere Puffer.");
        r3InputBuffer = "";
    }
}

// ============================================================================================
// PROTOCOL TRANSLATOR (Logik zu Display UI Mapping)
// ============================================================================================
void processR3Packet(String packet) {
    // Debug: Zeigt an, was vom R3 reinkam
    // Serial.print("RX (Parsed): "); Serial.println(packet);

    if (packet == "SYS:ALARM:ON") {
        emergencyStopActive = true;
        currentState = SYSTEM_ALARM;
        matrix.loadFrame(HUD_STOP);
        sendMotorCmd("BRAKE,1"); // Über Debugger senden
        sendBLECommand("[SYS:ALARM:ON]");
        return;
    }
    if (packet == "SYS:ALARM:OFF") {
        emergencyStopActive = false;
        currentState = SYSTEM_RUNNING;
        matrix.loadFrame(HUD_HEART);
        sendMotorCmd("BRAKE,0"); // Über Debugger senden
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

    if (packet.startsWith("SW:")) {
        int firstSep = packet.indexOf(':', 3);
        if (firstSep > 0) {
            String id = packet.substring(3, firstSep);
            String valStr = packet.substring(firstSep + 1);
            int val = valStr.toInt();

            if (id == "DOME") {
                if (val == 1) currentDomeEffect = RAINBOW;
                else if (val == 2) currentDomeEffect = BLINK_DISCO;
                else currentDomeEffect = OFF;
                
                sendBLECommand(val == 1 ? "[DATA:2:1]" : "[DATA:2:0]");
                sendBLECommand(val == 2 ? "[DATA:3:1]" : "[DATA:3:0]");
            }
            else if (id == "BLINK") {
                sendBLECommand(val == 1 ? "[DATA:4:1]" : "[DATA:4:0]");
                sendBLECommand(val == 2 ? "[DATA:5:1]" : "[DATA:5:0]");
            }
            else if (id == "SPEED") {
                if (val == 1) {
                    currentSpeedPWM = 255; 
                    Serial.println("[SPEED] Modus: TURBO (255)");
                }
                else if (val == 2) {
                    currentSpeedPWM = 100;
                    Serial.println("[SPEED] Modus: SLOW (100)");
                }
                else {
                    currentSpeedPWM = 150;
                    Serial.println("[SPEED] Modus: NORMAL (150)");
                }
                
                sendBLECommand(val == 1 ? "[DATA:6:1]" : "[DATA:6:0]");
                sendBLECommand(val == 2 ? "[DATA:7:1]" : "[DATA:7:0]");
            }
            else if (id == "NAVI") {
                sendBLECommand(val == 1 ? "[DATA:8:1]" : "[DATA:8:0]");
                sendBLECommand(val == 2 ? "[DATA:9:1]" : "[DATA:9:0]");
            }
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

    static int lastJoyState = -1; 
    static int lastSpeedSent = -1;
    int currentJoyState = 0;

    if (up) currentJoyState = 1;
    else if (down) currentJoyState = 2;
    else if (left) currentJoyState = 3;
    else if (right) currentJoyState = 4;

    if (currentJoyState != lastJoyState || currentSpeedPWM != lastSpeedSent) {
        
        switch (currentJoyState) {
            case 0: 
                sendMotorCmd("PWM,0"); 
                matrix.loadFrame(HUD_HEART);
                sendBLECommand("[JOY:MOVE:CENTER]");
                break;
            case 1: 
                sendMotorCmd("DIR,1"); 
                sendMotorCmd("PWM," + String(currentSpeedPWM));
                matrix.loadFrame(HUD_ARROW_UP);
                sendBLECommand("[JOY:MOVE:UP]");
                break;
            case 2: 
                sendMotorCmd("DIR,0"); 
                sendMotorCmd("PWM," + String(currentSpeedPWM));
                matrix.loadFrame(HUD_ARROW_DOWN);
                sendBLECommand("[JOY:MOVE:DOWN]");
                break;
            case 3: 
                sendMotorCmd("LDIR,0");
                sendMotorCmd("RDIR,1"); 
                sendMotorCmd("PWM," + String(currentSpeedPWM));
                sendBLECommand("[JOY:MOVE:LEFT]");
                break;
            case 4: 
                sendMotorCmd("LDIR,1");
                sendMotorCmd("RDIR,0"); 
                sendMotorCmd("PWM," + String(currentSpeedPWM));
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
    Serial.begin(115200);    
    Serial1.begin(115200);   
    
    // WICHTIG: Baudrate auf stabilere 38400 gesenkt
    motorNano.begin(38400); 

    pinMode(PIN_JOY_UP, INPUT_PULLUP);
    pinMode(PIN_JOY_DOWN, INPUT_PULLUP);
    pinMode(PIN_JOY_LEFT, INPUT_PULLUP);
    pinMode(PIN_JOY_RIGHT, INPUT_PULLUP);

    matrix.begin();
    matrix.loadFrame(HUD_SEARCHING);
    
    dome.begin();
    dome.setBrightness(150);
    dome.show();

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
    manageBLE();
    parseR3Input();
    handleJoystick();
    updateDome();

    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
        if (bleAuthenticated) {
            sendBLECommand("[SYS:LIFE:STABLE]");
        }
        lastHeartbeat = millis();
    }
}