/**
 * ============================================================================================
 * GITTERTIER PRO - ULTIMATE DISPLAY OS v24.1 (FULL PROTOCOL SUPPORT & NO-SIGNAL FALLBACK)
 * ============================================================================================
 * Hardware: ESP32-S3 (JC8048W550) 800x480 RGB
 * * PROTOKOLLUNTERSTUETZUNG:
 * - [DATA:PIN:VAL]   -> Aktualisiert Pin-Status (z.B. [DATA:4:1])
 * - [SW:ID:VAL]      -> Logische Schalter (DOME, BLINK, SPEED, NAVI, AUTO)
 * - [JOY:MOVE:DIR]   -> Joystick (UP/DOWN/LEFT/RIGHT/CENTER/IDLE)
 * - [SYS:ALARM:ON/OFF] -> Not-Aus
 * - [SYS:PWR:START/REQ_OFF] -> Power Management
 * - [SYS:LIFE:...]   -> Heartbeat
 * - AUTH|KEY         -> Authentifizierung
 * * DESIGN:
 * - Sehr große Schalter-Kacheln (170x110) in 4x2 Grid (Bildschirmfuellend).
 * - Standby: Pechschwarz, komplett neu entwickelter schwebender Farb-Pulsar-Text.
 * - Zero-Flicker durch strikte Dirty-Flags auf Pixelebene.
 * - NO-SIGNAL ACTION: Faellt der Heartbeat weg, wird sofort alles auf AUS gezwungen.
 * ============================================================================================
 */

#include <Arduino_GFX_Library.h>
#include <ArduinoBLE.h>
#include <deque>
#include <math.h>

// =========================================================================
// 1. HARDWARE KONFIGURATION (JC8048W550 24-Pin RGB)
// =========================================================================
#define GFX_BL 2
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    GFX_NOT_DEFINED, GFX_NOT_DEFINED, GFX_NOT_DEFINED,
    40, 41, 39, 42, 45, 48, 47, 21, 14, 5, 6, 7, 15, 16, 4, 8, 3, 46, 9, 1, false
);
Arduino_RPi_DPI_RGBPanel *gfx = new Arduino_RPi_DPI_RGBPanel(
    bus, 800, 0, 8, 4, 8, 480, 0, 8, 4, 8, 1, 16000000, true
);

// =========================================================================
// 2. FARBEN (APPLE DESIGN SYSTEM + EXTENDED)
// =========================================================================
#define C_BLACK        0x0000
#define C_WHITE        0xFFFF
#define C_BG_PAGE      0xF7BE      // #F2F2F7
#define C_CARD_BG      0xFFFF
#define C_PRIMARY      0x03DF      // #007AFF
#define C_ACCENT       0x5AB7      // #5856D6
#define C_SUCCESS      0x05E8      // #34C759
#define C_ERROR        0xF800      // #FF3B30
#define C_WARN         0xFD20      // #FF9500
#define C_TEXT_MAIN    0x0000
#define C_TEXT_SUB     0x8C71      // #8E8E93
#define C_BORDER       0xE73C
#define C_SHADOW       0xD6BA
#define C_DARK_GRAY    0x6B6D
#define C_LIGHT_BLUE   0x8E3F
#define C_GOLD         0xFE60      // #FFD700
#define C_TEAL         0x04EF      // #30B0C0
#define C_PINK         0xF81F      // #FF69B4
#define C_ORANGE       0xFD60      // #FF5E00

// =========================================================================
// 3. SYSTEM-STRUKTUREN & DATENMODELLE
// =========================================================================
struct PinMapping {
    int pin;                // Physischer oder virtueller Pin (2-9)
    String name;            // Name auf der großen Kachel
    String description;     // Subtext/Erklaerung
    uint16_t color;         // Themenfarbe wenn aktiv
    bool active;            // Aktueller Schaltzustand
    unsigned long lastChange; // Zeitpunkt des letzten Wechsels
    bool dirty;             // Render-Flag fuer Zero-Flicker
};

struct JoystickData {
    String direction;       // "UP", "DOWN", "LEFT", "RIGHT", "CENTER", "IDLE"
    bool moving;
    uint16_t color;
    unsigned long lastMove;
    int moveCount;
    bool dirty;
};

struct LogEntry {
    String text;
    uint16_t color;
    unsigned long timestamp;
};

struct SystemData {
    PinMapping pins[10];            // Array fuer Kacheln (Index 2 bis 9)
    JoystickData joystick;
    
    // Logische Schalterzustaende
    int domeState;      // 0=aus, 1=Rainbow, 2=Disco
    int blinkerState;   // 0=aus, 1=links, 2=rechts
    int speedState;     // 0=normal, 1=Turbo, 2=Langsam
    int naviState;      // 0=aus, 1=Auto, 2=Navi
    bool autonomActive; // Wahr wenn autonom fahrend
    
    // Verbindungs- & Sicherheitszustaende
    bool bleConnected;
    bool authenticated;
    bool emergencyStop;
    bool powerOn;
    bool shutdownRequested;
    bool noSignalMode;  // Wahr wenn Verbindung/Heartbeat abgebrochen ist
    
    // Timer und Counter
    unsigned long lastHeartbeat;
    unsigned long uptimeStart;
    
    int totalCommands;
    int joyMoves;
    int switchEvents;
};

SystemData sys;

// =========================================================================
// 4. EVENT-LOG & SPEICHER
// =========================================================================
#define MAX_LOG 10
std::deque<LogEntry> eventLog;

// Funktion zum Schreiben von Logs ins Display und Serial
void addLog(String text, uint16_t color = C_TEXT_SUB) {
    LogEntry entry;
    entry.text = text;
    entry.color = color;
    entry.timestamp = millis();
    eventLog.push_back(entry);
    if (eventLog.size() > MAX_LOG) eventLog.pop_front();
    Serial.println("LOG: " + text);
}

// =========================================================================
// 5. BLE KONFIGURATION
// =========================================================================
BLEService displayService("19b10000-e8f2-537e-4f6c-d104768a1214");
BLEStringCharacteristic dataChar("19b10001-e8f2-537e-4f6c-d104768a1214",
                                 BLERead | BLEWrite | BLENotify, 512);

// =========================================================================
// 6. ZUSTANDSMASCHINE & RENDER FLAGS
// =========================================================================
enum AppState {
    STATE_BOOT,
    STATE_SEARCHING,
    STATE_AUTH,
    STATE_DASHBOARD,
    STATE_STANDBY,
    STATE_ALARM,
    STATE_SHUTDOWN_MODAL
};

AppState currentState = STATE_BOOT;
AppState prevState = STATE_BOOT;
bool forceFullRedraw = true;
bool dashboardDirty = true;
bool logDirty = true;

// =========================================================================
// 7. HELFERFUNKTIONEN (UI & ZEIT)
// =========================================================================
String formatTime(unsigned long ms) {
    unsigned long sec = ms / 1000;
    unsigned long min = sec / 60;
    unsigned long hr = min / 60;
    sec %= 60; min %= 60;
    char buf[12];
    sprintf(buf, "%02lu:%02lu:%02lu", hr, min, sec);
    return String(buf);
}

// Zeichnet eine moderne abgerundete Kachel mit weichem Schatten-Ersatz
void drawGlassCard(int x, int y, int w, int h, int radius = 15) {
    gfx->fillRoundRect(x+3, y+3, w, h, radius, C_SHADOW);
    gfx->fillRoundRect(x, y, w, h, radius, C_CARD_BG);
    gfx->drawRoundRect(x, y, w, h, radius, C_BORDER);
}

// Zentrierter oder linksbuendiger Text
void drawText(int x, int y, String text, int size, uint16_t color, bool centered = false) {
    gfx->setTextSize(size);
    gfx->setTextColor(color);
    if (centered) {
        int16_t x1, y1;
        uint16_t w, h;
        gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        gfx->setCursor(x - w/2, y - h/2);
    } else {
        gfx->setCursor(x, y);
    }
    gfx->print(text);
}

// =========================================================================
// 8. INITIALISIERUNG & KACHEL-MAPPING
// =========================================================================
void initHardware() {
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
    gfx->begin();
    gfx->fillScreen(C_BLACK);
    
    sys.uptimeStart = millis();
    sys.lastHeartbeat = millis();
    sys.joystick.direction = "CENTER";
    sys.joystick.color = C_DARK_GRAY;
    sys.joystick.moveCount = 0;
    sys.noSignalMode = false;
    
    // Zuweisungen auf Basis von PDF Seite 6 / Vorheriger Logik
    // Diese Zuweisungen sind strikt an die Pins 2-9 gebunden.
    
    // Pin 2: DOME L (Rainbow)
    sys.pins[2].pin = 2;
    sys.pins[2].name = "DOME L";
    sys.pins[2].description = "Rainbow Mode";
    sys.pins[2].color = C_PRIMARY;
    sys.pins[2].active = false;
    sys.pins[2].dirty = false;
    
    // Pin 3: DOME R (Disco)
    sys.pins[3].pin = 3;
    sys.pins[3].name = "DOME R";
    sys.pins[3].description = "Disco Mode";
    sys.pins[3].color = C_ACCENT;
    sys.pins[3].active = false;
    sys.pins[3].dirty = false;
    
    // Pin 4: BLINK L
    sys.pins[4].pin = 4;
    sys.pins[4].name = "BLINK L";
    sys.pins[4].description = "Blinker Links";
    sys.pins[4].color = C_WARN;
    sys.pins[4].active = false;
    sys.pins[4].dirty = false;
    
    // Pin 5: BLINK R
    sys.pins[5].pin = 5;
    sys.pins[5].name = "BLINK R";
    sys.pins[5].description = "Blinker Rechts";
    sys.pins[5].color = C_WARN;
    sys.pins[5].active = false;
    sys.pins[5].dirty = false;
    
    // Pin 6: SPEED FAST
    sys.pins[6].pin = 6;
    sys.pins[6].name = "TURBO";
    sys.pins[6].description = "Fast Speed";
    sys.pins[6].color = C_ERROR;
    sys.pins[6].active = false;
    sys.pins[6].dirty = false;
    
    // Pin 7: SPEED SLOW
    sys.pins[7].pin = 7;
    sys.pins[7].name = "SLOW";
    sys.pins[7].description = "Turtle Speed";
    sys.pins[7].color = C_TEAL;
    sys.pins[7].active = false;
    sys.pins[7].dirty = false;
    
    // Pin 8: AUTO
    sys.pins[8].pin = 8;
    sys.pins[8].name = "AUTO";
    sys.pins[8].description = "Autonom Fahren";
    sys.pins[8].color = C_GOLD;
    sys.pins[8].active = false;
    sys.pins[8].dirty = false;
    
    // Pin 9: NAVI
    sys.pins[9].pin = 9;
    sys.pins[9].name = "NAVI";
    sys.pins[9].description = "Navigation On";
    sys.pins[9].color = C_SUCCESS;
    sys.pins[9].active = false;
    sys.pins[9].dirty = false;
    
    sys.domeState = 0;
    sys.blinkerState = 0;
    sys.speedState = 0;
    sys.naviState = 0;
    sys.autonomActive = false;
    
    addLog("Hardware init abgeschlossen", C_SUCCESS);
}

void initBLE() {
    if (!BLE.begin()) {
        Serial.println("BLE Fehler!");
        while(1);
    }
    BLE.setLocalName("Gittertier Pro");
    BLE.setAdvertisedService(displayService);
    displayService.addCharacteristic(dataChar);
    BLE.addService(displayService);
    BLE.advertise();
    addLog("BLE Advertising bereit", C_LIGHT_BLUE);
}

// =========================================================================
// 8.5 FALLBACK-ROUTINE: NO SIGNAL (PDF SEITE 6)
// =========================================================================
void triggerNoSignalFallbacks() {
    if (sys.noSignalMode) return; // Verhindert Spamming
    
    sys.noSignalMode = true;
    addLog("CRITICAL: KEIN SIGNAL! Alles auf AUS.", C_ERROR);
    
    // Alle Kacheln hart auf AUS setzen (Aktion bei "Kein Signal")
    for (int i = 2; i <= 9; i++) {
        if (sys.pins[i].active) {
            sys.pins[i].active = false;
            sys.pins[i].dirty = true;
            sys.pins[i].lastChange = millis();
        }
    }
    
    // Logik-Zustaende zuruecksetzen
    sys.domeState = 0;
    sys.blinkerState = 0;
    sys.speedState = 0;
    sys.naviState = 0;
    sys.autonomActive = false;
    
    // Sofortiger Dashboard Redraw, um das UI zu aktualisieren
    dashboardDirty = true;
}

void restoreSignal() {
    if (sys.noSignalMode) {
        sys.noSignalMode = false;
        addLog("Signal wiederhergestellt.", C_SUCCESS);
        dashboardDirty = true;
    }
}

// =========================================================================
// 9. BEFEHLSPARSER (FIXED WHITESPACE BUG)
// =========================================================================
void parseR4Command(String cmd) {
    cmd.trim(); // Bereinige eventuelle Zeilenumbrueche
    sys.totalCommands++;
    Serial.println("R4->Display: " + cmd);
    
    // 1. Authentifizierung
    if (cmd.startsWith("AUTH|")) {
        String key = cmd.substring(5);
        if (key == "GITTER_77_PRO_SEC") {
            sys.authenticated = true;
            sys.bleConnected = true;
            sys.lastHeartbeat = millis(); // Setze Heartbeat zurueck
            restoreSignal();
            currentState = STATE_DASHBOARD;
            forceFullRedraw = true;
            addLog("Auth OK - Verbunden", C_SUCCESS);
            dataChar.writeValue("AUTH_OK");
        } else {
            addLog("Auth fehlgeschlagen", C_ERROR);
        }
        return;
    }
    
    // 2. Eckige Klammern entfernen & erneut trimmen (Loest den DATA:4:0 Fehler!)
    if (cmd.startsWith("[") && cmd.endsWith("]")) {
        cmd = cmd.substring(1, cmd.length() - 1);
        cmd.trim(); // ENTSCHEIDENDER FIX: Verhindert, dass "[ DATA:4:0]" zu " DATA:4:0" wird
    }
    
    // Wenn wir ab hier sind, haben wir ein valides Signal. 
    sys.lastHeartbeat = millis(); 
    restoreSignal();
    
    if (!sys.authenticated) return;
    
    // ========== DATA: PIN STATUS ==========
    if (cmd.startsWith("DATA:")) {
        int firstColon = cmd.indexOf(':');
        int secondColon = cmd.indexOf(':', firstColon+1);
        if (firstColon >= 0 && secondColon > firstColon) {
            String pinStr = cmd.substring(firstColon+1, secondColon);
            String valStr = cmd.substring(secondColon+1);
            
            // Trim zur Sicherheit
            pinStr.trim();
            valStr.trim();
            
            int pin = pinStr.toInt();
            int val = valStr.toInt();
            
            if (pin >= 2 && pin <= 9) {
                bool oldActive = sys.pins[pin].active;
                sys.pins[pin].active = (val == 1);
                
                if (oldActive != sys.pins[pin].active) {
                    sys.pins[pin].lastChange = millis();
                    sys.pins[pin].dirty = true;
                    sys.switchEvents++;
                    
                    String state = sys.pins[pin].active ? "ON" : "OFF";
                    addLog(sys.pins[pin].name + " -> " + state, sys.pins[pin].active ? sys.pins[pin].color : C_DARK_GRAY);
                    dashboardDirty = true;
                    logDirty = true;
                    
                    // Modalfreigabe testen
                    if (currentState == STATE_SHUTDOWN_MODAL && sys.shutdownRequested) {
                        bool stillBlocked = false;
                        for (int i = 2; i <= 9; i++) {
                            if (sys.pins[i].active) { stillBlocked = true; break; }
                        }
                        if (!stillBlocked) {
                            sys.powerOn = false;
                            currentState = STATE_STANDBY;
                            forceFullRedraw = true;
                            addLog("Shutdown freigegeben", C_SUCCESS);
                        } else {
                            forceFullRedraw = true; // Refresh Modal
                        }
                    }
                }
            }
        }
        return;
    }
    
    // ========== JOYSTICK ==========
    if (cmd.startsWith("JOY:MOVE:")) {
        String dir = cmd.substring(9);
        dir.trim();
        sys.joystick.direction = dir;
        sys.joystick.moving = (dir != "CENTER" && dir != "IDLE");
        sys.joystick.lastMove = millis();
        sys.joystick.dirty = true;
        sys.joyMoves++;
        sys.joystick.moveCount++;
        
        if (dir == "UP") sys.joystick.color = C_SUCCESS;
        else if (dir == "DOWN") sys.joystick.color = C_WARN;
        else if (dir == "LEFT") sys.joystick.color = C_ACCENT;
        else if (dir == "RIGHT") sys.joystick.color = C_PRIMARY;
        else sys.joystick.color = C_DARK_GRAY;
        
        addLog("JOY: " + dir, sys.joystick.color);
        dashboardDirty = true;
        logDirty = true;
        return;
    }
    
    // ========== ALARM ==========
    if (cmd.startsWith("SYS:ALARM:")) {
        String state = cmd.substring(10);
        state.trim();
        if (state == "ON") {
            sys.emergencyStop = true;
            prevState = currentState;
            currentState = STATE_ALARM;
            forceFullRedraw = true;
            addLog("NOT-AUS AKTIVIERT!", C_ERROR);
        } else if (state == "OFF") {
            sys.emergencyStop = false;
            currentState = prevState;
            forceFullRedraw = true;
            addLog("Not-Aus deeskaliert", C_SUCCESS);
        }
        return;
    }
    
    // ========== POWER MANAGEMENT ==========
    if (cmd.startsWith("SYS:PWR:")) {
        String pwr = cmd.substring(8);
        pwr.trim();
        if (pwr == "START") {
            sys.powerOn = true;
            currentState = STATE_DASHBOARD;
            forceFullRedraw = true;
            addLog("System START", C_SUCCESS);
        } else if (pwr == "REQ_OFF") {
            sys.shutdownRequested = true;
            bool blocked = false;
            for (int i = 2; i <= 9; i++) {
                if (sys.pins[i].active) { blocked = true; break; }
            }
            if (blocked) {
                prevState = currentState;
                currentState = STATE_SHUTDOWN_MODAL;
                forceFullRedraw = true;
                addLog("Shutdown blockiert (Schalter an)", C_WARN);
            } else {
                sys.powerOn = false;
                currentState = STATE_STANDBY;
                forceFullRedraw = true;
                addLog("System fahrt in Standby", C_SUCCESS);
            }
        }
        return;
    }
    
    // ========== HEARTBEAT ==========
    if (cmd.startsWith("SYS:LIFE:")) {
        sys.lastHeartbeat = millis();
        restoreSignal();
        // Nicht jeden Heartbeat loggen, sonst spammt es den kleinen Log zu.
        static int hbCnt = 0;
        if (++hbCnt % 50 == 0) {
            addLog("Heartbeat empfangen", C_LIGHT_BLUE);
            logDirty = true;
        }
        return;
    }
    
    // ========== LOGISCHE SCHALTER (SW:ID:VAL) ==========
    if (cmd.startsWith("SW:")) {
        int firstColon = cmd.indexOf(':');
        int secondColon = cmd.indexOf(':', firstColon+1);
        if (firstColon >= 0 && secondColon > firstColon) {
            String swType = cmd.substring(firstColon+1, secondColon);
            int val = cmd.substring(secondColon+1).toInt();
            swType.trim();
            
            bool changed = false;
            if (swType == "DOME") {
                if (sys.domeState != val) changed = true;
                sys.domeState = val;
                bool pin2 = (val == 1);
                bool pin3 = (val == 2);
                if (sys.pins[2].active != pin2) { sys.pins[2].active = pin2; sys.pins[2].dirty = true; dashboardDirty = true; }
                if (sys.pins[3].active != pin3) { sys.pins[3].active = pin3; sys.pins[3].dirty = true; dashboardDirty = true; }
                if (changed) addLog("DOME Modus: " + String(val), C_PRIMARY);
            }
            else if (swType == "BLINK") {
                if (sys.blinkerState != val) changed = true;
                sys.blinkerState = val;
                bool pin4 = (val == 1);
                bool pin5 = (val == 2);
                if (sys.pins[4].active != pin4) { sys.pins[4].active = pin4; sys.pins[4].dirty = true; dashboardDirty = true; }
                if (sys.pins[5].active != pin5) { sys.pins[5].active = pin5; sys.pins[5].dirty = true; dashboardDirty = true; }
                if (changed) addLog("BLINK: " + String(val), C_WARN);
            }
            else if (swType == "SPEED") {
                if (sys.speedState != val) changed = true;
                sys.speedState = val;
                bool pin6 = (val == 1);
                bool pin7 = (val == 2);
                if (sys.pins[6].active != pin6) { sys.pins[6].active = pin6; sys.pins[6].dirty = true; dashboardDirty = true; }
                if (sys.pins[7].active != pin7) { sys.pins[7].active = pin7; sys.pins[7].dirty = true; dashboardDirty = true; }
                if (changed) addLog("SPEED: " + String(val), C_ACCENT);
            }
            else if (swType == "NAVI") {
                if (sys.naviState != val) changed = true;
                sys.naviState = val;
                bool pin8 = (val == 1);
                bool pin9 = (val == 2);
                if (sys.pins[8].active != pin8) { sys.pins[8].active = pin8; sys.pins[8].dirty = true; dashboardDirty = true; }
                if (sys.pins[9].active != pin9) { sys.pins[9].active = pin9; sys.pins[9].dirty = true; dashboardDirty = true; }
                if (changed) addLog("NAVI: " + String(val), C_TEAL);
            }
            else if (swType == "AUTO") {
                bool newActive = (val == 1);
                if (sys.autonomActive != newActive) changed = true;
                sys.autonomActive = newActive;
                if (changed) addLog("AUTONOM State: " + String(val), C_GOLD);
            }
            if(changed) logDirty = true;
        }
        return;
    }
    
    // Falls das Kommando komplett unbekannt ist
    addLog("Unbekannt: " + cmd, C_ERROR);
    logDirty = true;
}

// =========================================================================
// 10. BLE EVENT HANDLING & SIGNAL UEBERWACHUNG
// =========================================================================
void handleBLE() {
    BLE.poll();
    BLEDevice central = BLE.central();
    if (central) {
        if (!sys.bleConnected) {
            sys.bleConnected = true;
            addLog("BLE verbunden: " + central.address(), C_SUCCESS);
        }
        if (dataChar.written()) {
            String cmd = dataChar.value();
            parseR4Command(cmd);
        }
    } else {
        if (sys.bleConnected) {
            sys.bleConnected = false;
            sys.authenticated = false;
            triggerNoSignalFallbacks(); // Sofort alle Pins aus!
            addLog("BLE Verbindung ABGEBROCHEN", C_ERROR);
            if (currentState != STATE_BOOT && currentState != STATE_STANDBY) {
                currentState = STATE_SEARCHING;
            }
            forceFullRedraw = true;
        }
    }
}

// =========================================================================
// 11. ZEICHENFUNKTIONEN FUER DIE UI-ZUSTAENDE
// =========================================================================
void drawBoot() {
    if (forceFullRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        drawGlassCard(200, 140, 400, 200, 25);
        drawText(400, 180, "GITTERTIER", 4, C_PRIMARY, true);
        drawText(400, 230, "PRO OS", 5, C_ACCENT, true);
        drawText(400, 300, "Version 24.1 (No-Signal Core)", 2, C_TEXT_SUB, true);
        gfx->fillRoundRect(200, 360, 400, 20, 10, C_BORDER);
        forceFullRedraw = false;
    }
    static int prog = 0;
    static unsigned long last = 0;
    if (millis() - last > 15) {
        prog += 3;
        if (prog > 400) prog = 400;
        gfx->fillRoundRect(201, 361, prog-2, 18, 8, C_PRIMARY);
        last = millis();
        if (prog >= 400) {
            delay(200);
            currentState = STATE_SEARCHING;
            forceFullRedraw = true;
        }
    }
}

void drawSearching() {
    if (forceFullRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        drawText(400, 80, "Verbindung wird aufgebaut", 3, C_TEXT_MAIN, true);
        drawGlassCard(150, 130, 500, 240, 20);
        drawText(400, 170, "SECURITY ID:", 2, C_TEXT_SUB, true);
        drawText(400, 210, "GITTER_77_PRO_SEC", 3, C_PRIMARY, true);
        drawText(400, 280, "Warte auf R3 Master...", 2, C_TEXT_SUB, true);
        
        if (sys.noSignalMode) {
             drawText(400, 330, "NO-SIGNAL FALLBACK AKTIV (ALLES AUS)", 2, C_ERROR, true);
        }
        forceFullRedraw = false;
    }
    
    // Animierter Ladekreis
    static float angle = 0;
    int cx = 400, cy = 400, r = 50;
    int oldX = cx + r * cos((angle-6)*PI/180);
    int oldY = cy + r * sin((angle-6)*PI/180);
    gfx->drawLine(cx, cy, oldX, oldY, C_BG_PAGE);
    angle += 6; if (angle>=360) angle=0;
    int newX = cx + r * cos(angle*PI/180);
    int newY = cy + r * sin(angle*PI/180);
    gfx->drawLine(cx, cy, newX, newY, C_PRIMARY);
    gfx->drawCircle(cx, cy, r, C_PRIMARY);
    
    if (sys.bleConnected && !sys.authenticated) {
        currentState = STATE_AUTH;
        forceFullRedraw = true;
    }
}

void drawAuthenticating() {
    if (forceFullRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        drawText(400, 100, "Sicherer Schluesselaustausch", 3, C_TEXT_MAIN, true);
        drawGlassCard(200, 150, 400, 200, 20);
        drawText(400, 200, "Validiere Token...", 2, C_TEXT_SUB, true);
        for (int i=0; i<5; i++) gfx->fillCircle(320+i*40, 280, 12, C_DARK_GRAY);
        forceFullRedraw = false;
    }
    static int dot=0; static unsigned long last=0;
    if (millis()-last > 150) {
        gfx->fillCircle(320+dot*40, 280, 12, C_DARK_GRAY);
        dot = (dot+1)%5;
        gfx->fillCircle(320+dot*40, 280, 12, C_PRIMARY);
        last = millis();
    }
    if (sys.authenticated) {
        currentState = STATE_DASHBOARD;
        forceFullRedraw = true;
    }
}

// =========================================================================
// 12. DASHBOARD (GROESSERE KACHELN, ZERO FLICKER)
// =========================================================================
void drawDashboard() {
    // LAYOUT KALIBRIERUNG fuer 800x480
    const int headerH = 60;
    const int gridStartY = headerH + 15;
    
    // Groeßere Kacheln (4 Spalten, 2 Reihen)
    const int switchW = 175;
    const int switchH = 110;
    const int gapX = 15;
    const int gapY = 15;
    const int gridX = (800 - (4*switchW + 3*gapX)) / 2; // = (800 - 745)/2 = 27
    
    const int logY = gridStartY + 2*switchH + gapY + 15; // 75 + 220 + 15 + 15 = 325
    const int logH = 480 - logY - 15; // 480 - 325 - 15 = 140
    
    if (forceFullRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        
        // Header
        gfx->fillRect(0, 0, 800, headerH, C_CARD_BG);
        gfx->drawFastHLine(0, headerH, 800, C_BORDER);
        drawText(20, 20, "GITTERTIER PRO", 3, C_PRIMARY);
        
        // Status Indikatoren
        String status = sys.noSignalMode ? "NO SIGNAL" : (sys.bleConnected ? "ONLINE" : "OFFLINE");
        uint16_t statusCol = sys.noSignalMode ? C_ERROR : (sys.bleConnected ? C_SUCCESS : C_WARN);
        drawText(680, 15, status, 2, statusCol);
        
        // Uptime
        drawText(680, 40, formatTime(millis() - sys.uptimeStart), 1, C_TEXT_SUB);
        
        // Joystick Mini-Indikator
        drawGlassCard(590, 8, 70, 45, 10);
        drawText(625, 30, sys.joystick.direction.substring(0,1), 3, sys.joystick.color, true);
        
        // Schalter-Grid zeichnen (4x2)
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 4; col++) {
                int idx = row * 4 + col;
                int pin = 2 + idx;
                int x = gridX + col * (switchW + gapX);
                int y = gridStartY + row * (switchH + gapY);
                
                drawGlassCard(x, y, switchW, switchH, 18);
                
                // Kachel Titel
                drawText(x + switchW/2, y + 25, sys.pins[pin].name, 2, C_TEXT_MAIN, true);
                
                // Status zeichnen
                String state = sys.pins[pin].active ? "ON" : "OFF";
                uint16_t stateColor = sys.pins[pin].active ? sys.pins[pin].color : C_DARK_GRAY;
                
                // Fuellkreis als Indikator
                gfx->fillCircle(x + switchW/2 - 30, y + 70, 8, stateColor);
                drawText(x + switchW/2 + 10, y + 70, state, 3, stateColor, true);
                
                sys.pins[pin].dirty = false;
            }
        }
        
        // Event-Log Container
        drawGlassCard(gridX, logY, 800 - 2*gridX, logH, 15);
        drawText(gridX + 20, logY + 15, "SYSTEM LOG (LIVE)", 1, C_TEXT_SUB);
        
        forceFullRedraw = false;
        dashboardDirty = true;
        logDirty = true;
    }
    
    // PARTIELLE UPDATES (ZERO FLICKER)
    if (dashboardDirty || (millis() - sys.lastHeartbeat > 1000)) {
        
        // Top-Leiste Uptime und Status
        gfx->fillRect(675, 10, 120, 45, C_CARD_BG);
        String status = sys.noSignalMode ? "NO SIGNAL" : (sys.bleConnected ? "ONLINE" : "OFFLINE");
        uint16_t statusCol = sys.noSignalMode ? C_ERROR : (sys.bleConnected ? C_SUCCESS : C_WARN);
        drawText(680, 15, status, 2, statusCol);
        drawText(680, 40, formatTime(millis() - sys.uptimeStart), 1, C_TEXT_SUB);
        
        // Joystick
        if (sys.joystick.dirty) {
            gfx->fillRoundRect(593, 11, 64, 39, 8, C_CARD_BG);
            drawText(625, 30, sys.joystick.direction.substring(0,1), 3, sys.joystick.color, true);
            sys.joystick.dirty = false;
        }
        
        // Kacheln selektiv updaten
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 4; col++) {
                int pin = 2 + (row * 4 + col);
                if (sys.pins[pin].dirty) {
                    int x = gridX + col * (switchW + gapX);
                    int y = gridStartY + row * (switchH + gapY);
                    
                    // Nur den unteren Bereich der Kachel (Status) ueberschreiben
                    gfx->fillRect(x + 5, y + 50, switchW - 10, 50, C_CARD_BG);
                    
                    String state = sys.pins[pin].active ? "ON" : "OFF";
                    uint16_t stateColor = sys.pins[pin].active ? sys.pins[pin].color : C_DARK_GRAY;
                    
                    gfx->fillCircle(x + switchW/2 - 30, y + 70, 8, stateColor);
                    drawText(x + switchW/2 + 10, y + 70, state, 3, stateColor, true);
                    
                    sys.pins[pin].dirty = false;
                }
            }
        }
        
        // Event-Log selektiv updaten
        if (logDirty) {
            gfx->fillRect(gridX + 15, logY + 30, (800 - 2*gridX) - 30, logH - 40, C_CARD_BG);
            int lineY = logY + 35;
            for (size_t i = 0; i < eventLog.size(); i++) {
                String timeStr = formatTime(eventLog[i].timestamp);
                String txt = timeStr + "  |  " + eventLog[i].text;
                if (txt.length() > 65) txt = txt.substring(0, 62) + "...";
                drawText(gridX + 25, lineY, txt, 1, eventLog[i].color);
                lineY += 18;
                if (lineY > logY + logH - 20) break;
            }
            logDirty = false;
        }
        
        dashboardDirty = false;
    }
    
    // Joystick Auto-Release (Nach 1.2s ohne Kommando auf CENTER)
    if (sys.joystick.moving && (millis() - sys.joystick.lastMove > 1200)) {
        sys.joystick.moving = false;
        sys.joystick.direction = "CENTER";
        sys.joystick.color = C_DARK_GRAY;
        sys.joystick.dirty = true;
        dashboardDirty = true;
    }
}

// =========================================================================
// 13. STANDBY (MODERN ANIMIERT, SCHWARZ)
// =========================================================================
// Partikelsystem fuer den exklusiven Standby-Look
struct Particle {
    float x, y;
    float vx, vy;
    uint16_t color;
};
Particle particles[40];
bool particlesInit = false;

void drawStandby() {

    // Moderner Standby: Schwarzer Hintergrund + animierter "Gittertier Pro" Text

    if (forceFullRedraw) {

        gfx->fillScreen(C_BLACK);

        forceFullRedraw = false;

    }

   

    static float hue = 0;

    static unsigned long lastAnim = 0;

    if (millis() - lastAnim > 50) {

        hue += 0.02;

        if (hue > 1) hue -= 1;

        lastAnim = millis();

    }

   

    // Text "Gittertier Pro" mit Regenbogen-Farbverlauf

    String txt = "Gittertier Pro";

    int txtW = txt.length() * 36;  // geschaetzte Breite bei size=3

    int startX = 400 - txtW/2;

    for (int i = 0; i < txt.length(); i++) {

        float charHue = hue + i * 0.05;

        uint16_t col = gfx->color565(

            (int)(sin(charHue * 2 * PI) * 127 + 128),

            (int)(sin((charHue + 0.33) * 2 * PI) * 127 + 128),

            (int)(sin((charHue + 0.66) * 2 * PI) * 127 + 128)

        );

        drawText(startX + i * 36, 220, String(txt[i]), 6, col);

    }

   

    // Subtiler Puls-Effekt (Groeße aendern)

    static float scale = 0.8;

    static bool growing = true;

    static unsigned long lastScale = 0;

    if (millis() - lastScale > 30) {

        if (growing) scale += 0.02;

        else scale -= 0.02;

        if (scale >= 1.2) growing = false;

        if (scale <= 0.9) growing = true;

        lastScale = millis();

    }

   

    // Hinweis-Text

    drawText(400, 350, "STANDBY - Druecke POWER", 1, C_TEXT_SUB, true);

    //drawText(400, 380, formatTime(millis() - sys.uptimeStart), 1, C_TEXT_SUB, true);

   

    delay(30);

}

// =========================================================================
// 14. ALARM & SHUTDOWN-MODAL
// =========================================================================
void drawAlarm() {
    static bool flash = false;
    static unsigned long lastFlash = 0;
    if (millis() - lastFlash > 150) {
        flash = !flash;
        if (flash) {
            gfx->fillScreen(C_ERROR);
            drawText(400, 200, "!!! NOT-AUS !!!", 6, C_WHITE, true);
            drawText(400, 300, "SYSTEM WURDE GESTOPPT", 3, C_WHITE, true);
            drawText(400, 350, "Warte auf Freigabe (ALARM:OFF)", 2, C_WHITE, true);
        } else {
            gfx->fillScreen(C_BLACK);
            drawText(400, 200, "!!! NOT-AUS !!!", 6, C_ERROR, true);
            drawText(400, 300, "SYSTEM WURDE GESTOPPT", 3, C_ERROR, true);
            drawText(400, 350, "Warte auf Freigabe (ALARM:OFF)", 2, C_ERROR, true);
        }
        lastFlash = millis();
    }
}

void drawShutdownModal() {
    if (forceFullRedraw) {
        // Pseudo-Transparenz durch Streifen (Scanlines)
        for (int y = 0; y < 480; y += 3) {
            gfx->drawFastHLine(0, y, 800, C_BLACK);
        }
        drawGlassCard(150, 100, 500, 300, 20);
        drawText(400, 140, "SHUTDOWN BLOCKIERT", 3, C_ERROR, true);
        drawText(400, 180, "Folgende Schalter sind noch aktiv:", 2, C_TEXT_MAIN, true);
        
        int yPos = 220;
        for (int pin = 2; pin <= 9; pin++) {
            if (sys.pins[pin].active) {
                String txt = "-> " + sys.pins[pin].name;
                drawText(200, yPos, txt, 2, sys.pins[pin].color);
                yPos += 30;
            }
        }
        drawText(400, yPos + 30, "Bitte alle Kacheln auf OFF stellen.", 2, C_TEXT_SUB, true);
        forceFullRedraw = false;
    }
}

// =========================================================================
// 15. MAIN LOOP & SIGNALUEBERWACHUNG
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== GITTERTIER PRO v24.1 BOOT SEQUENCE ===");
    
    initHardware();
    initBLE();
    
    currentState = STATE_BOOT;
    forceFullRedraw = true;
}

void loop() {
    // 1. BLE Events verarbeiten
    handleBLE();
    
    // 2. Heartbeat & Signalueberwachung pruefen (NO SIGNAL FALLBACK)
    // Wenn 8 Sekunden lang kein Heartbeat mehr kam und wir verbunden waren
    if (sys.bleConnected && sys.authenticated && !sys.noSignalMode) {
        if (millis() - sys.lastHeartbeat > 8000) {
            triggerNoSignalFallbacks();
        }
    }
    
    // 3. UI Zeichnen basierend auf State
    switch (currentState) {
        case STATE_BOOT:           drawBoot();           break;
        case STATE_SEARCHING:      drawSearching();      break;
        case STATE_AUTH:           drawAuthenticating(); break;
        case STATE_DASHBOARD:      drawDashboard();      break;
        case STATE_STANDBY:        drawStandby();        break;
        case STATE_ALARM:          drawAlarm();          break;
        case STATE_SHUTDOWN_MODAL: drawShutdownModal();  break;
    }
    
    // Kurze Pause fuer Systemstabilitaet
    delay(10);
}