/**
 * GITTERLIER PRO - OPTIMIERTES DISPLAY OS v22.0
 * =====================================================
 * OPTIMIERUNGEN:
 * - Echtzeit-Datenverarbeitung aller empfangenen BLE-Kommandos
 * - Detailliertes Event-Logging für alle Ereignisse
 * - Live-Visualisierung von Joystick-Bewegungen
 * - Windows-Style Shutdown mit Schalter-Prüfung
 * - Alarm-System mit Rot-Überlagerung
 * - System-Heartbeat Monitoring
 * =====================================================
 */

#include <Arduino_GFX_Library.h>
#include <ArduinoBLE.h>
#include <vector>
#include <deque>

// =========================================================================
// 1. HARDWARE KONFIGURATION
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
// 2. DESIGN SYSTEM
// =========================================================================
#define C_BLACK        0x0000
#define C_WHITE        0xFFFF
#define C_BG_PAGE      0xF7BE
#define C_CARD_BG      0xFFFF
#define C_PRIMARY      0x03DF
#define C_ACCENT       0x5AB7
#define C_SUCCESS      0x05E8
#define C_ERROR        0xF800
#define C_WARN         0xFD20
#define C_TEXT_MAIN    0x0000
#define C_TEXT_SUB     0x8C71
#define C_BORDER       0xE73C
#define C_SHADOW       0xD6BA
#define C_DARK_GRAY    0x6B6D
#define C_LIGHT_BLUE   0x8E3F
#define C_DARK_BLUE    0x015F

// =========================================================================
// 3. EVENT SYSTEM
// =========================================================================
struct SystemEvent {
    String type;        // "JOYSTICK", "SWITCH", "POWER", "ALARM", "AUTH", "HEARTBEAT"
    String details;     // Details der Aktion
    String value;       // Wert oder Status
    uint16_t color;     // Farbe für Anzeige
    unsigned long timestamp;
    bool important;     // Wichtige Ereignisse (für Log)
};

#define MAX_EVENTS 20
std::deque<SystemEvent> eventLog;

// =========================================================================
// 4. HARDWARE STATUS
// =========================================================================
struct SwitchStatus {
    bool active;
    String name;
    String description;
    uint16_t color;
    int pin;
    unsigned long lastChange;
};

struct JoystickStatus {
    String direction;
    bool centered;
    uint16_t color;
    unsigned long lastMove;
    bool moving;
};

struct SystemStatus {
    // Schalter Pins 2-9
    SwitchStatus switches[10];
    
    // Joystick
    JoystickStatus joystick;
    
    // System State
    bool bleConnected;
    bool authenticated;
    bool emergencyStop;
    bool powerOn;
    bool shutdownRequested;
    
    // Power Management
    bool checkShutdownConstraints;
    String shutdownBlockedBy;
    
    // Timers
    unsigned long lastHeartbeat;
    unsigned long lastDataUpdate;
    unsigned long startupTime;
    
    // Counters
    int totalCommands;
    int joystickMoves;
    int switchToggles;
    int powerEvents;
    int alarmEvents;
};

SystemStatus sys;

// =========================================================================
// 5. BLE KONFIGURATION
// =========================================================================
BLEService displayService("19b10000-e8f2-537e-4f6c-d104768a1214");
BLEStringCharacteristic dataCharacteristic("19b10001-e8f2-537e-4f6c-d104768a1214", 
                                         BLERead | BLEWrite | BLENotify, 512);

// =========================================================================
// 6. UI STATE
// =========================================================================
enum DisplayState {
    STATE_BOOT,
    STATE_SEARCHING,
    STATE_AUTHENTICATING,
    STATE_DASHBOARD,
    STATE_STANDBY,
    STATE_ALARM,
    STATE_SHUTDOWN_MODAL
};

DisplayState currentState = STATE_BOOT;
DisplayState previousState = STATE_BOOT;
bool forceRedraw = true;

// =========================================================================
// 7. EVENT LOGGING FUNKTIONEN
// =========================================================================
void logEvent(String type, String details, String value = "", uint16_t color = C_TEXT_SUB, bool important = false) {
    SystemEvent event;
    event.type = type;
    event.details = details;
    event.value = value;
    event.color = color;
    event.timestamp = millis();
    event.important = important;
    
    eventLog.push_back(event);
    
    // Begrenze Log auf MAX_EVENTS
    if (eventLog.size() > MAX_EVENTS) {
        eventLog.pop_front();
    }
    
    Serial.println("EVENT: " + type + " - " + details + " [" + value + "]");
}

String formatTime(unsigned long ms) {
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    seconds %= 60;
    unsigned long hours = minutes / 60;
    minutes %= 60;
    
    char buffer[12];
    sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(buffer);
}

// =========================================================================
// 8. HARDWARE INITIALISIERUNG
// =========================================================================
void initializeHardware() {
    // Display initialisieren
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
    gfx->begin();
    gfx->fillScreen(C_BLACK);
    
    // System Status initialisieren
    memset(&sys, 0, sizeof(sys));
    
    sys.startupTime = millis();
    sys.powerOn = false;
    sys.emergencyStop = false;
    sys.shutdownRequested = false;
    sys.checkShutdownConstraints = false;
    
    // Joystick initialisieren
    sys.joystick.direction = "WAITING";
    sys.joystick.centered = true;
    sys.joystick.color = C_DARK_GRAY;
    sys.joystick.moving = false;
    sys.joystick.lastMove = 0;
    
    // Schalter initialisieren (Pins 2-9)
    for (int i = 2; i <= 9; i++) {
        sys.switches[i].active = false;
        sys.switches[i].pin = i;
        sys.switches[i].lastChange = 0;
        sys.switches[i].color = C_DARK_GRAY;
    }
    
    // Schalter-Namen gemäß Kabelverteilung
    sys.switches[2].name = "SW-TR-L";  // Top Right Left
    sys.switches[2].description = "Schalter oben rechts links";
    sys.switches[2].color = C_PRIMARY;
    
    sys.switches[3].name = "SW-TR-R";  // Top Right Right
    sys.switches[3].description = "Schalter oben rechts rechts";
    sys.switches[3].color = C_PRIMARY;
    
    sys.switches[4].name = "SW-TL-L";  // Top Left Left
    sys.switches[4].description = "Schalter oben links links";
    sys.switches[4].color = C_PRIMARY;
    
    sys.switches[5].name = "SW-TL-R";  // Top Left Right
    sys.switches[5].description = "Schalter oben links rechts";
    sys.switches[5].color = C_PRIMARY;
    
    sys.switches[6].name = "SW-BR-L";  // Bottom Right Left
    sys.switches[6].description = "Schalter unten rechts links";
    sys.switches[6].color = C_PRIMARY;
    
    sys.switches[7].name = "SW-BR-R";  // Bottom Right Right
    sys.switches[7].description = "Schalter unten rechts rechts";
    sys.switches[7].color = C_PRIMARY;
    
    sys.switches[8].name = "SW-BL-L";  // Bottom Left Left
    sys.switches[8].description = "Schalter unten links links";
    sys.switches[8].color = C_PRIMARY;
    
    sys.switches[9].name = "SW-BL-R";  // Bottom Left Right
    sys.switches[9].description = "Schalter unten links rechts";
    sys.switches[9].color = C_PRIMARY;
    
    // Event Log initialisieren
    eventLog.clear();
    logEvent("SYSTEM", "Hardware initialisiert", "BOOT", C_SUCCESS, true);
}

// =========================================================================
// 9. BLE INITIALISIERUNG
// =========================================================================
void initializeBLE() {
    if (!BLE.begin()) {
        Serial.println("BLE Initialisierung fehlgeschlagen!");
        gfx->fillScreen(C_ERROR);
        gfx->setTextColor(C_WHITE);
        gfx->setTextSize(3);
        gfx->setCursor(100, 200);
        gfx->print("BLE FEHLER!");
        while(1);
    }
    
    BLE.setLocalName("Gittertier Pro");
    BLE.setDeviceName("Gittertier Pro Display");
    BLE.setAdvertisedService(displayService);
    
    displayService.addCharacteristic(dataCharacteristic);
    BLE.addService(displayService);
    
    // Start Advertising
    BLE.advertise();
    
    logEvent("BLE", "Advertising gestartet", "Gittertier Pro", C_SUCCESS, true);
    Serial.println("BLE Advertising gestartet");
    Serial.println("Name: Gittertier Pro");
    Serial.println("UUID: 19b10000-e8f2-537e-4f6c-d104768a1214");
}

// =========================================================================
// 10. BLE KOMMANDO PARSER (OPTIMIERT)
// =========================================================================
void parseBLECommand(String command) {
    command.trim();
    sys.totalCommands++;
    
    Serial.print("Empfangen: ");
    Serial.println(command);
    
    // Authentifizierung
    if (command.startsWith("AUTH|")) {
        String authKey = command.substring(5);
        if (authKey == "GITTER_77_PRO_SEC") {
            sys.authenticated = true;
            sys.bleConnected = true;
            currentState = STATE_DASHBOARD;
            forceRedraw = true;
            
            logEvent("AUTH", "Authentifizierung erfolgreich", authKey, C_SUCCESS, true);
            dataCharacteristic.writeValue("AUTH_OK");
        } else {
            logEvent("AUTH", "Authentifizierung fehlgeschlagen", authKey, C_ERROR, true);
        }
        return;
    }
    
    // Nur verarbeiten wenn authentifiziert
    if (!sys.authenticated) {
        return;
    }
    
    // Entferne Klammern wenn vorhanden
    if (command.startsWith("[") && command.endsWith("]")) {
        command = command.substring(1, command.length() - 1);
    }
    
    // Joystick Bewegung
    if (command.startsWith("JOY:MOVE:")) {
        String direction = command.substring(9);
        direction.trim();
        
        sys.joystick.direction = direction;
        sys.joystick.moving = (direction != "IDLE" && direction != "CENTER");
        sys.joystick.centered = (direction == "IDLE" || direction == "CENTER");
        sys.joystick.lastMove = millis();
        sys.joystickMoves++;
        
        // Farben basierend auf Richtung
        if (direction == "UP") sys.joystick.color = C_SUCCESS;
        else if (direction == "DOWN") sys.joystick.color = C_WARN;
        else if (direction == "LEFT") sys.joystick.color = C_ACCENT;
        else if (direction == "RIGHT") sys.joystick.color = C_PRIMARY;
        else sys.joystick.color = C_DARK_GRAY;
        
        logEvent("JOYSTICK", "Bewegung erkannt", direction, sys.joystick.color);
        return;
    }
    
    // Alarm System
    if (command.startsWith("SYS:ALARM:")) {
        String alarmState = command.substring(10);
        
        if (alarmState == "ON") {
            sys.emergencyStop = true;
            sys.alarmEvents++;
            previousState = currentState;
            currentState = STATE_ALARM;
            forceRedraw = true;
            
            logEvent("ALARM", "NOT-AUS aktiviert", "EMERGENCY STOP", C_ERROR, true);
        } else if (alarmState == "OFF") {
            sys.emergencyStop = false;
            currentState = previousState;
            forceRedraw = true;
            
            logEvent("ALARM", "NOT-AUS deaktiviert", "System normal", C_SUCCESS);
        }
        return;
    }
    
    // Power Management
    if (command.startsWith("SYS:PWR:")) {
        String powerCmd = command.substring(8);
        
        if (powerCmd == "START") {
            sys.powerOn = true;
            sys.powerEvents++;
            currentState = STATE_DASHBOARD;
            forceRedraw = true;
            
            logEvent("POWER", "System gestartet", "BOOT COMPLETE", C_SUCCESS, true);
        } else if (powerCmd == "REQ_OFF") {
            sys.shutdownRequested = true;
            sys.checkShutdownConstraints = true;
            sys.powerEvents++;
            
            // Prüfe ob Schalter aktiv sind
            bool blocked = false;
            sys.shutdownBlockedBy = "";
            
            for (int i = 2; i <= 9; i++) {
                if (sys.switches[i].active) {
                    blocked = true;
                    if (!sys.shutdownBlockedBy.isEmpty()) {
                        sys.shutdownBlockedBy += ", ";
                    }
                    sys.shutdownBlockedBy += sys.switches[i].name;
                }
            }
            
            if (blocked) {
                previousState = currentState;
                currentState = STATE_SHUTDOWN_MODAL;
                forceRedraw = true;
                
                logEvent("POWER", "Shutdown blockiert", sys.shutdownBlockedBy, C_WARN, true);
            } else {
                sys.powerOn = false;
                currentState = STATE_STANDBY;
                forceRedraw = true;
                
                logEvent("POWER", "System heruntergefahren", "STANDBY", C_SUCCESS, true);
            }
        }
        return;
    }
    
    // Heartbeat
    if (command.startsWith("SYS:LIFE:")) {
        String heartbeat = command.substring(9);
        sys.lastHeartbeat = millis();
        
        // Optional: Heartbeat im Log anzeigen
        static int heartbeatCount = 0;
        heartbeatCount++;
        if (heartbeatCount % 10 == 0) {  // Nur jede 10. Heartbeat loggen
            logEvent("HEARTBEAT", "System aktiv", heartbeat, C_LIGHT_BLUE);
        }
        return;
    }
    
    // Schalter Daten [DATA:PIN:VAL]
    if (command.startsWith("DATA:")) {
        int firstColon = command.indexOf(':');
        int lastColon = command.lastIndexOf(':');
        
        if (firstColon >= 0 && lastColon > firstColon) {
            String pinStr = command.substring(firstColon + 1, lastColon);
            String valStr = command.substring(lastColon + 1);
            
            int pin = pinStr.toInt();
            bool value = (valStr.toInt() == 1);
            
            if (pin >= 2 && pin <= 9) {
                bool changed = (sys.switches[pin].active != value);
                sys.switches[pin].active = value;
                sys.switches[pin].lastChange = millis();
                
                if (changed) {
                    sys.switchToggles++;
                    String state = value ? "AKTIV" : "INAKTIV";
                    uint16_t color = value ? sys.switches[pin].color : C_DARK_GRAY;
                    
                    logEvent("SWITCH", sys.switches[pin].name + " geändert", state, color);
                    
                    // Wenn Shutdown blockiert war, prüfe ob jetzt möglich
                    if (currentState == STATE_SHUTDOWN_MODAL && sys.shutdownRequested) {
                        bool stillBlocked = false;
                        sys.shutdownBlockedBy = "";
                        
                        for (int i = 2; i <= 9; i++) {
                            if (sys.switches[i].active) {
                                stillBlocked = true;
                                if (!sys.shutdownBlockedBy.isEmpty()) {
                                    sys.shutdownBlockedBy += ", ";
                                }
                                sys.shutdownBlockedBy += sys.switches[i].name;
                            }
                        }
                        
                        if (!stillBlocked) {
                            sys.powerOn = false;
                            currentState = STATE_STANDBY;
                            forceRedraw = true;
                            logEvent("POWER", "Shutdown jetzt möglich", "STANDBY", C_SUCCESS);
                        } else {
                            forceRedraw = true; // Modal neu zeichnen
                        }
                    }
                }
            }
        }
        return;
    }
    
    // Unbekanntes Kommando
    logEvent("UNKNOWN", "Unbekanntes Kommando", command, C_ERROR);
}

// =========================================================================
// 11. SMART UI RENDERING FUNKTIONEN
// =========================================================================
void drawGlassCard(int x, int y, int w, int h, int radius = 20) {
    // Schatten
    gfx->fillRoundRect(x + 4, y + 4, w, h, radius, C_SHADOW);
    // Hauptkarte
    gfx->fillRoundRect(x, y, w, h, radius, C_CARD_BG);
    // Border
    gfx->drawRoundRect(x, y, w, h, radius, C_BORDER);
}

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
// 12. STATE HANDLER FUNKTIONEN
// =========================================================================
void handleStateBoot() {
    if (forceRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        
        // Logo
        drawGlassCard(250, 140, 300, 200);
        drawText(400, 180, "GITTERLIER", 4, C_PRIMARY, true);
        drawText(400, 230, "PRO", 6, C_ACCENT, true);
        drawText(400, 300, "v22.0", 2, C_TEXT_SUB, true);
        
        // Ladebalken
        gfx->fillRoundRect(200, 360, 400, 20, 10, C_BORDER);
        
        forceRedraw = false;
    }
    
    // Ladebalken Animation
    static int progress = 0;
    static unsigned long lastUpdate = 0;
    
    if (millis() - lastUpdate > 30) {
        progress += 2;
        if (progress > 400) progress = 400;
        
        gfx->fillRoundRect(201, 361, progress - 2, 18, 8, C_PRIMARY);
        lastUpdate = millis();
    }
    
    // Übergang
    if (progress >= 400) {
        delay(500);
        currentState = STATE_SEARCHING;
        forceRedraw = true;
    }
}

void handleStateSearching() {
    if (forceRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        
        drawText(400, 100, "Suche Kasten...", 3, C_TEXT_MAIN, true);
        drawGlassCard(150, 150, 500, 200);
        
        // Security ID
        drawText(400, 180, "SECURITY ID:", 2, C_TEXT_SUB, true);
        drawText(400, 220, "GITTER_77_PRO_SEC", 3, C_PRIMARY, true);
        
        // BLE Status
        drawText(400, 280, "BLE Advertising aktiv", 2, C_TEXT_SUB, true);
        
        forceRedraw = false;
    }
    
    // Radar Animation
    static float angle = 0;
    int centerX = 400;
    int centerY = 400;
    int radius = 60;
    
    // Lösche alten Strahl
    int oldX = centerX + radius * cos((angle - 5) * PI / 180);
    int oldY = centerY + radius * sin((angle - 5) * PI / 180);
    gfx->drawLine(centerX, centerY, oldX, oldY, C_BG_PAGE);
    
    // Zeichne neuen Strahl
    angle += 5;
    if (angle >= 360) angle = 0;
    
    int newX = centerX + radius * cos(angle * PI / 180);
    int newY = centerY + radius * sin(angle * PI / 180);
    gfx->drawLine(centerX, centerY, newX, newY, C_PRIMARY);
    
    // Radar Kreise
    gfx->drawCircle(centerX, centerY, radius, C_PRIMARY);
    gfx->drawCircle(centerX, centerY, radius/2, C_PRIMARY);
    
    // Verbunden?
    if (sys.bleConnected && !sys.authenticated) {
        currentState = STATE_AUTHENTICATING;
        forceRedraw = true;
    }
}

void handleStateAuthenticating() {
    if (forceRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        
        drawText(400, 100, "Authentifizierung", 3, C_TEXT_MAIN, true);
        drawGlassCard(150, 150, 500, 200);
        
        drawText(400, 200, "Warte auf Security Key...", 2, C_TEXT_SUB, true);
        
        // Punkte für Animation
        for (int i = 0; i < 6; i++) {
            gfx->fillCircle(300 + i * 40, 280, 10, C_DARK_GRAY);
        }
        
        forceRedraw = false;
    }
    
    // Punkt Animation
    static int activeDot = 0;
    static unsigned long lastDot = 0;
    
    if (millis() - lastDot > 200) {
        // Vorherigen Punkt zurücksetzen
        gfx->fillCircle(300 + activeDot * 40, 280, 10, C_DARK_GRAY);
        
        // Nächsten Punkt aktivieren
        activeDot = (activeDot + 1) % 6;
        gfx->fillCircle(300 + activeDot * 40, 280, 10, C_PRIMARY);
        
        lastDot = millis();
    }
    
    // Wenn authentifiziert
    if (sys.authenticated) {
        currentState = STATE_DASHBOARD;
        forceRedraw = true;
    }
}

void handleStateDashboard() {
    static unsigned long lastUpdate = 0;
    
    // Initiales Rendering
    if (forceRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        
        // HEADER
        gfx->fillRect(0, 0, 800, 70, C_CARD_BG);
        gfx->drawFastHLine(0, 70, 800, C_BORDER);
        
        drawText(30, 25, "GITTERLIER PRO - LIVE DASHBOARD", 2, C_PRIMARY);
        
        // Status LED
        String statusText = sys.bleConnected ? "ONLINE" : "OFFLINE";
        uint16_t statusColor = sys.bleConnected ? C_SUCCESS : C_ERROR;
        drawText(700, 25, statusText, 2, statusColor);
        
        // HAUPTINHALT
        // Linke Spalte: Joystick & Systeminfo
        drawGlassCard(20, 90, 350, 350);
        
        // Joystick Anzeige
        drawText(195, 120, "JOYSTICK STATUS", 2, C_TEXT_SUB, true);
        drawGlassCard(100, 150, 190, 100, 15);
        drawText(195, 200, sys.joystick.direction, 4, sys.joystick.color, true);
        
        // System Info
        drawText(195, 280, "SYSTEM INFO", 2, C_TEXT_SUB, true);
        drawGlassCard(100, 310, 190, 110, 10);
        
        // Rechte Spalte: Schalter Status
        drawGlassCard(390, 90, 390, 350);
        drawText(585, 120, "SCHALTER STATUS (PINS 2-9)", 2, C_TEXT_SUB, true);
        
        // Schalter Grid
        int switchX = 410;
        int switchY = 150;
        int switchW = 85;
        int switchH = 70;
        
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 4; col++) {
                int pin = 2 + row * 4 + col;
                int x = switchX + col * (switchW + 10);
                int y = switchY + row * (switchH + 10);
                
                drawGlassCard(x, y, switchW, switchH, 8);
                
                // Schalter Name
                drawText(x + switchW/2, y + 15, sys.switches[pin].name, 1, C_TEXT_MAIN, true);
                
                // Status
                String state = sys.switches[pin].active ? "ON" : "OFF";
                uint16_t stateColor = sys.switches[pin].active ? sys.switches[pin].color : C_DARK_GRAY;
                drawText(x + switchW/2, y + 40, state, 2, stateColor, true);
            }
        }
        
        // UNTERE Sektion: Event Log
        drawGlassCard(20, 460, 760, 280);
        drawText(400, 480, "LIVE EVENT LOG", 2, C_TEXT_SUB, true);
        
        forceRedraw = false;
    }
    
    // Dynamische Updates (nur alle 100ms)
    if (millis() - lastUpdate > 100) {
        // Joystick aktualisieren
        if (sys.joystick.moving && (millis() - sys.joystick.lastMove > 2000)) {
            sys.joystick.moving = false;
            sys.joystick.color = C_DARK_GRAY;
        }
        
        // Joystick Anzeige aktualisieren
        gfx->fillRect(101, 151, 188, 98, C_CARD_BG);
        drawText(195, 200, sys.joystick.direction, 4, sys.joystick.color, true);
        
        // Schalter Status aktualisieren
        int switchX = 410;
        int switchY = 150;
        int switchW = 85;
        int switchH = 70;
        
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 4; col++) {
                int pin = 2 + row * 4 + col;
                int x = switchX + col * (switchW + 10);
                int y = switchY + row * (switchH + 10);
                
                // Status
                String state = sys.switches[pin].active ? "ON" : "OFF";
                uint16_t stateColor = sys.switches[pin].active ? sys.switches[pin].color : C_DARK_GRAY;
                
                // Statusfeld löschen
                gfx->fillRect(x + 10, y + 35, switchW - 20, 25, C_CARD_BG);
                drawText(x + switchW/2, y + 40, state, 2, stateColor, true);
            }
        }
        
        // System Info aktualisieren
        gfx->fillRect(101, 311, 188, 108, C_CARD_BG);
        
        int infoY = 330;
        drawText(195, infoY, "Commands: " + String(sys.totalCommands), 1, C_TEXT_MAIN, true);
        infoY += 20;
        drawText(195, infoY, "Joystick: " + String(sys.joystickMoves), 1, C_TEXT_MAIN, true);
        infoY += 20;
        drawText(195, infoY, "Switches: " + String(sys.switchToggles), 1, C_TEXT_MAIN, true);
        infoY += 20;
        drawText(195, infoY, "Uptime: " + formatTime(millis()), 1, C_TEXT_MAIN, true);
        
        // Event Log anzeigen
        gfx->fillRect(30, 500, 740, 230, C_CARD_BG);
        
        int logY = 510;
        int maxEvents = 6;
        int startIdx = (eventLog.size() > maxEvents) ? eventLog.size() - maxEvents : 0;
        
        for (size_t i = startIdx; i < eventLog.size() && (logY < 700); i++) {
            const SystemEvent& event = eventLog[i];
            
            // Zeitstempel
            String timeStr = formatTime(event.timestamp);
            drawText(40, logY, timeStr, 1, C_TEXT_SUB);
            
            // Event Type
            drawText(150, logY, event.type, 1, event.color);
            
            // Details
            String displayText = event.details;
            if (!event.value.isEmpty()) {
                displayText += " [" + event.value + "]";
            }
            
            // Text kürzen wenn zu lang
            if (displayText.length() > 40) {
                displayText = displayText.substring(0, 37) + "...";
            }
            
            drawText(300, logY, displayText, 1, C_TEXT_MAIN);
            
            logY += 25;
        }
        
        // Footer mit letztem Update
        String lastUpdateStr = "Last Update: " + String(millis() - sys.lastDataUpdate) + "ms ago";
        drawText(400, 740, lastUpdateStr, 1, C_TEXT_SUB, true);
        
        lastUpdate = millis();
        sys.lastDataUpdate = millis();
    }
}

void handleStateStandby() {
    static unsigned long lastPlasma = 0;
    
    if (forceRedraw) {
        gfx->fillScreen(C_BLACK);
        forceRedraw = false;
    }
    
    // Plasma Effekt
    if (millis() - lastPlasma > 50) {
        for (int y = 0; y < 480; y += 20) {
            for (int x = 0; x < 800; x += 20) {
                float value = sin(x * 0.01 + millis() * 0.001) * 0.5 +
                            sin(y * 0.01 + millis() * 0.0007) * 0.3 +
                            sin((x + y) * 0.01 + millis() * 0.0005) * 0.2;
                
                value = (value + 1.0) * 0.5;
                
                uint16_t color;
                if (value < 0.33) color = gfx->color565(0, 122 * value * 3, 255);
                else if (value < 0.66) color = gfx->color565(88, 86, 214);
                else color = gfx->color565(175, 82, 222);
                
                gfx->fillRect(x, y, 20, 20, color);
            }
        }
        lastPlasma = millis();
    }
    
    // Standby Text
    drawText(400, 200, "STANDBY MODUS", 4, C_WHITE, true);
    drawText(400, 250, "System ist im Energiesparmodus", 2, C_TEXT_SUB, true);
    drawText(400, 280, "Drücke POWER zum Aktivieren", 2, C_TEXT_SUB, true);
    
    // Uptime anzeigen
    drawText(400, 350, "Uptime: " + formatTime(millis()), 2, C_LIGHT_BLUE, true);
}

void handleStateAlarm() {
    static bool flashState = false;
    static unsigned long lastFlash = 0;
    
    if (millis() - lastFlash > 200) {
        flashState = !flashState;
        
        if (flashState) {
            gfx->fillScreen(C_ERROR);
            drawText(400, 200, "!!! NOT-AUS !!!", 5, C_WHITE, true);
            drawText(400, 280, "Sicherheitskette unterbrochen", 3, C_WHITE, true);
            drawText(400, 320, "System gestoppt", 2, C_WHITE, true);
        } else {
            gfx->fillScreen(C_BLACK);
            drawText(400, 200, "!!! NOT-AUS !!!", 5, C_ERROR, true);
            drawText(400, 280, "Sicherheitskette unterbrochen", 3, C_ERROR, true);
            drawText(400, 320, "System gestoppt", 2, C_ERROR, true);
        }
        
        lastFlash = millis();
    }
    
    // Exit Information
    drawText(400, 400, "Warte auf ALARM:OFF Signal", 2, C_WARN, true);
}

void handleStateShutdownModal() {
    if (forceRedraw) {
        // Overlay Hintergrund (halbdurchsichtig schwarz)
        for (int y = 0; y < 480; y += 2) {
            gfx->drawFastHLine(0, y, 800, C_BLACK);
        }
        
        // Modal Fenster
        drawGlassCard(150, 120, 500, 240);
        
        // Titel
        drawText(400, 150, "SHUTDOWN BLOCKIERT!", 3, C_ERROR, true);
        
        // Nachricht
        drawText(400, 200, "Aktive Schalter gefunden:", 2, C_TEXT_MAIN, true);
        
        // Liste aktiver Schalter
        int yPos = 230;
        int activeCount = 0;
        
        for (int i = 2; i <= 9; i++) {
            if (sys.switches[i].active) {
                String switchText = "• " + sys.switches[i].name + " (PIN " + String(i) + ")";
                drawText(170, yPos, switchText, 2, sys.switches[i].color);
                yPos += 30;
                activeCount++;
            }
        }
        
        // Anweisung
        drawText(400, yPos + 20, "Bitte alle Schalter deaktivieren", 2, C_TEXT_SUB, true);
        drawText(400, yPos + 50, "Automatischer Übergang zu STANDBY", 1, C_SUCCESS, true);
        
        forceRedraw = false;
    }
    
    // Prüfe ob noch Schalter aktiv sind
    bool stillBlocked = false;
    for (int i = 2; i <= 9; i++) {
        if (sys.switches[i].active) {
            stillBlocked = true;
            break;
        }
    }
    
    // Wenn keine Schalter mehr aktiv, gehe zu Standby
    if (!stillBlocked) {
        sys.powerOn = false;
        currentState = STATE_STANDBY;
        forceRedraw = true;
        logEvent("POWER", "Shutdown jetzt möglich", "Wechsel zu STANDBY", C_SUCCESS);
    }
}

// =========================================================================
// 13. BLE EVENT HANDLER
// =========================================================================
void handleBLEEvents() {
    BLE.poll();
    
    // Verbindungsstatus
    BLEDevice central = BLE.central();
    
    if (central) {
        if (!sys.bleConnected) {
            sys.bleConnected = true;
            logEvent("BLE", "Verbunden mit Kasten", central.address(), C_SUCCESS, true);
        }
        
        // Daten lesen
        if (dataCharacteristic.written()) {
            String receivedData = dataCharacteristic.value();
            parseBLECommand(receivedData);
        }
    } else {
        if (sys.bleConnected) {
            sys.bleConnected = false;
            sys.authenticated = false;
            logEvent("BLE", "Verbindung verloren", "Reconnecting...", C_ERROR, true);
            
            if (currentState != STATE_BOOT && currentState != STATE_STANDBY) {
                currentState = STATE_SEARCHING;
                forceRedraw = true;
            }
        }
    }
}

// =========================================================================
// 14. SETUP & LOOP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== GITTERLIER PRO OPTIMIZED DASHBOARD v22.0 ===");
    Serial.println("ESP32-S3 JC8048W550 800x480 RGB");
    Serial.println("BLE UUID: 19b10000-e8f2-537e-4f6c-d104768a1214");
    Serial.println("Security: GITTER_77_PRO_SEC");
    Serial.println("================================================\n");
    
    initializeHardware();
    initializeBLE();
    
    currentState = STATE_BOOT;
    forceRedraw = true;
}

void loop() {
    handleBLEEvents();
    
    switch (currentState) {
        case STATE_BOOT:
            handleStateBoot();
            break;
        case STATE_SEARCHING:
            handleStateSearching();
            break;
        case STATE_AUTHENTICATING:
            handleStateAuthenticating();
            break;
        case STATE_DASHBOARD:
            handleStateDashboard();
            break;
        case STATE_STANDBY:
            handleStateStandby();
            break;
        case STATE_ALARM:
            handleStateAlarm();
            break;
        case STATE_SHUTDOWN_MODAL:
            handleStateShutdownModal();
            break;
    }
    
    // Watchdog für Heartbeat
    static unsigned long lastHeartbeatCheck = 0;
    if (millis() - lastHeartbeatCheck > 10000) {  // Alle 10 Sekunden
        if (sys.bleConnected && sys.authenticated && 
            millis() - sys.lastHeartbeat > 15000) {  // 15 Sekunden Timeout
            logEvent("SYSTEM", "Heartbeat Timeout", "Reconnecting...", C_WARN, true);
            sys.bleConnected = false;
            sys.authenticated = false;
            currentState = STATE_SEARCHING;
            forceRedraw = true;
        }
        lastHeartbeatCheck = millis();
    }
    
    delay(10);
}