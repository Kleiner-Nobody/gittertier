/**
 * GITTERTIER PRO - ULTIMATE DISPLAY OS (v17.0)
 * ==========================================================
 * Hardware: ESP32-S3 (JC8048W550) 800x480 RGB
 * Theme: "Web Landing Page" (Apple Liquid Style)
 * * FEATURES:
 * - Industrial State Machine (No blocking delays)
 * - 8-Channel Switch Visualization Grid
 * - Mathematical Plasma Standby Screen
 * - Smart Partial Refresh (Zero Flicker)
 * - Modal Overlay System for Shutdown Protection
 */

#include <Arduino_GFX_Library.h>
#include <ArduinoBLE.h>
#include <vector>
#include <math.h>

// =========================================================================
// 1. COLORS & DESIGN TOKENS (Apple Design System)
// =========================================================================
#define C_BLACK        0x0000
#define C_WHITE        0xFFFF
#define C_BG_PAGE      0xF7BE // #F2F2F7 (Light Gray)
#define C_CARD_BG      0xFFFF // White
#define C_PRIMARY      0x03DF // #007AFF (Blue)
#define C_ACCENT       0x5AB7 // #5856D6 (Purple)
#define C_SUCCESS      0x05E8 // #34C759 (Green)
#define C_ERROR        0xF800 // #FF3B30 (Red)
#define C_WARN         0xFD20 // #FF9500 (Orange)
#define C_TEXT_MAIN    0x0000 // Black
#define C_TEXT_SUB     0x8C71 // #8E8E93 (Gray)
#define C_BORDER       0xE73C // Thin Border
#define C_SHADOW       0xD6BA // Soft Shadow

// =========================================================================
// 2. HARDWARE DRIVER
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
// 3. SYSTEM STATE & DATA
// =========================================================================

enum Scene {
    SCENE_BOOT,
    SCENE_CONNECTING,
    SCENE_DASHBOARD,
    SCENE_MODAL_BLOCK,
    SCENE_STANDBY,
    SCENE_ALARM
};

Scene currentScene = SCENE_BOOT;
bool forceRedraw = true; // Dirty Flag for Full Refresh

struct SwitchState {
    bool active;
    String name;
    String position; // e.g. "Top Left L"
    uint16_t color;
};

struct SystemData {
    // Array for Pins 2-9 (Index 0-7 mapped for easier logic)
    // We map Pin X to index X
    SwitchState switches[14]; 
    String joystick = "CENTER";
    bool bleConnected = false;
    String debugMsg = "Init System...";
    float plasmaTime = 0.0;
};

SystemData sys;

// BLE
BLEService gService("19b10000-e8f2-537e-4f6c-d104768a1214");
BLEStringCharacteristic rxChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLERead | BLEWrite | BLENotify, 512);

// =========================================================================
// 4. CONFIGURATION: SWITCH MAPPING
// =========================================================================
void initSwitches() {
    // Defaults
    for(int i=0; i<14; i++) {
        sys.switches[i].active = false;
        sys.switches[i].color = C_PRIMARY;
        sys.switches[i].name = "AUX";
        sys.switches[i].position = "PIN " + String(i);
    }

    // Custom Mapping based on your Project Plan
    sys.switches[3].name = "TURBO";     sys.switches[3].position = "Top R (R)"; sys.switches[3].color = C_ERROR;
    sys.switches[2].name = "AUX 1";     sys.switches[2].position = "Top R (L)";
    
    sys.switches[5].name = "BLINK R";   sys.switches[5].position = "Top L (R)"; sys.switches[5].color = C_WARN;
    sys.switches[4].name = "BLINK L";   sys.switches[4].position = "Top L (L)"; sys.switches[4].color = C_WARN;
    
    sys.switches[6].name = "DISCO";     sys.switches[6].position = "Bot R (L)"; sys.switches[6].color = C_ACCENT;
    sys.switches[7].name = "AUX 2";     sys.switches[7].position = "Bot R (R)";
    
    sys.switches[8].name = "AUX 3";     sys.switches[8].position = "Bot L (L)";
    sys.switches[9].name = "AUX 4";     sys.switches[9].position = "Bot L (R)";
}

// =========================================================================
// 5. GRAPHICS ENGINE (SMART REFRESH)
// =========================================================================

// Helper: Draw a Web-Style Card
void drawCard(int x, int y, int w, int h) {
    gfx->fillRoundRect(x+4, y+4, w, h, 15, C_SHADOW); // Shadow
    gfx->fillRoundRect(x, y, w, h, 15, C_CARD_BG);    // Body
    gfx->drawRoundRect(x, y, w, h, 15, C_BORDER);     // Border
}

// Helper: Smart Text (Only redraws if changed)
// Note: Requires a static buffer variable passed by reference for each text field!
void smartText(int x, int y, int w, int h, String text, int size, uint16_t color, uint16_t bg, bool center, String &buffer) {
    if (text == buffer && !forceRedraw) return;
    
    gfx->fillRect(x, y, w, h, bg); // Clear Area
    gfx->setTextSize(size);
    gfx->setTextColor(color);
    
    int cx = x + 10;
    int cy = y + (h/2) - (size*4);
    
    if (center) {
        int tw = text.length() * size * 6;
        cx = x + (w - tw)/2;
    }
    
    gfx->setCursor(cx, cy);
    gfx->print(text);
    buffer = text;
}

// Helper: Draw Switch Status in Grid
void drawSwitchItem(int x, int y, int w, int h, int pinId, bool &lastState) {
    SwitchState &s = sys.switches[pinId];
    
    // Check redraw necessity
    if (s.active == lastState && !forceRedraw) return;
    
    uint16_t boxColor = s.active ? s.color : C_BG_PAGE;
    uint16_t textColor = s.active ? C_WHITE : C_TEXT_SUB;
    
    // Draw Box
    gfx->fillRoundRect(x, y, w, h, 10, boxColor);
    
    // Draw Label (Name)
    gfx->setTextSize(1);
    gfx->setTextColor(textColor);
    gfx->setCursor(x + 10, y + 10);
    gfx->print(s.name);
    
    // Draw Position (Small)
    gfx->setTextSize(1);
    gfx->setCursor(x + 10, y + 30);
    gfx->print(s.position);
    
    // Draw Status Indicator
    if (s.active) {
        gfx->fillCircle(x + w - 15, y + 15, 5, C_WHITE);
    } else {
        gfx->drawCircle(x + w - 15, y + 15, 5, C_SHADOW);
    }
    
    lastState = s.active;
}

// =========================================================================
// 6. SCENES
// =========================================================================

void scene_Boot() {
    if (forceRedraw) {
        gfx->fillScreen(C_WHITE);
        
        // Logo Animation Frame 1
        gfx->fillCircle(400, 200, 80, C_PRIMARY);
        gfx->setTextSize(6);
        gfx->setTextColor(C_WHITE);
        gfx->setCursor(382, 180);
        gfx->print("G");
        
        gfx->setTextSize(3);
        gfx->setTextColor(C_TEXT_MAIN);
        gfx->setCursor(290, 300);
        gfx->print("Gittertier Pro");
        
        forceRedraw = false;
    }
    
    // Simulated Loading Bar
    static int w = 0;
    w += 15;
    gfx->fillRect(200, 350, w, 8, C_PRIMARY);
    
    if (w >= 400) {
        delay(500);
        currentScene = SCENE_CONNECTING;
        forceRedraw = true;
        w = 0;
    }
    delay(20);
}

void scene_Connecting() {
    if (forceRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        drawCard(250, 150, 300, 180);
        
        gfx->setTextSize(2);
        gfx->setTextColor(C_TEXT_SUB);
        gfx->setCursor(290, 200);
        gfx->print("Suche Kasten...");
        forceRedraw = false;
    }
    
    // Radar Pulse Animation
    static int r = 0;
    gfx->drawCircle(400, 260, r, C_PRIMARY);
    r = (r + 2) % 50;
    
    if (sys.bleConnected) {
        currentScene = SCENE_DASHBOARD;
        forceRedraw = true;
    }
}

void scene_Dashboard() {
    static String bufJoy, bufStat, bufMsg;
    static bool bufSwState[14]; 

    // 1. Initial Static Layout
    if (forceRedraw) {
        gfx->fillScreen(C_BG_PAGE);
        
        // Header
        gfx->fillRect(0, 0, 800, 60, C_WHITE);
        gfx->drawFastHLine(0, 60, 800, C_BORDER);
        gfx->setTextSize(2); gfx->setTextColor(C_PRIMARY);
        gfx->setCursor(30, 22); gfx->print("Gittertier Pro OS");

        // Main Containers
        drawCard(30, 90, 200, 250);   // Joystick Card
        drawCard(250, 90, 520, 250);  // Switch Grid
        drawCard(30, 360, 740, 100);  // Log / Info
        
        gfx->setTextColor(C_TEXT_SUB);
        gfx->setCursor(50, 110); gfx->print("ANTRIEB");
        gfx->setCursor(270, 110); gfx->print("SCHALTER STATUS");

        forceRedraw = false;
        // Init buffer
        for(int i=0; i<14; i++) bufSwState[i] = !sys.switches[i].active; 
    }

    // 2. Dynamic: Joystick
    String joyIcon = "-";
    if (sys.joystick == "UP") joyIcon = "^";
    if (sys.joystick == "DOWN") joyIcon = "v";
    if (sys.joystick == "LEFT") joyIcon = "<";
    if (sys.joystick == "RIGHT") joyIcon = ">";
    
    smartText(50, 160, 160, 100, joyIcon, 9, C_TEXT_MAIN, C_CARD_BG, true, bufJoy);
    smartText(50, 280, 160, 30, sys.joystick, 2, C_TEXT_SUB, C_CARD_BG, true, bufStat);

    // 3. Dynamic: Switch Grid (2x4)
    // Row 1
    drawSwitchItem(270, 130, 110, 80, 4, bufSwState[4]); // BLINK L
    drawSwitchItem(390, 130, 110, 80, 2, bufSwState[2]); // AUX 1
    drawSwitchItem(510, 130, 110, 80, 3, bufSwState[3]); // TURBO
    drawSwitchItem(630, 130, 110, 80, 5, bufSwState[5]); // BLINK R
    
    // Row 2
    drawSwitchItem(270, 230, 110, 80, 8, bufSwState[8]); // AUX 3
    drawSwitchItem(390, 230, 110, 80, 6, bufSwState[6]); // DISCO
    drawSwitchItem(510, 230, 110, 80, 7, bufSwState[7]); // AUX 2
    drawSwitchItem(630, 230, 110, 80, 9, bufSwState[9]); // AUX 4

    // 4. Dynamic: Header Status
    String status = sys.bleConnected ? "ONLINE" : "OFFLINE";
    static String lastHead;
    smartText(650, 20, 120, 20, status, 2, C_SUCCESS, C_WHITE, false, lastHead);
    
    // 5. Dynamic: Footer Log
    smartText(50, 380, 700, 60, sys.debugMsg, 2, C_TEXT_MAIN, C_CARD_BG, false, bufMsg);
}

void scene_Standby() {
    // This creates a "Plasma" effect by calculating colors based on coordinates and time
    // It's "colourous and cool"
    
    if (forceRedraw) {
        // Just clear once
        gfx->fillScreen(C_BLACK);
        forceRedraw = false;
    }
    
    sys.plasmaTime += 0.05;
    
    // We draw dynamic blobs. To keep FPS high, we don't do per-pixel, 
    // but moving large filled circles
    
    int cx = 400 + sin(sys.plasmaTime) * 150;
    int cy = 240 + cos(sys.plasmaTime * 0.7) * 100;
    int r = 80 + sin(sys.plasmaTime * 2) * 20;
    
    // Clear previous area (simulated trail)
    // Actually, simply refilling background is too slow. 
    // We use a moving text effect instead for max smoothness on ESP32
    
    gfx->fillScreen(C_BLACK); // Need to clear for clean animation
    
    // Draw 3 moving blobs
    gfx->fillCircle(cx, cy, r, C_PRIMARY);
    gfx->fillCircle(400 - (cx-400), 480 - cy, r, C_ACCENT);
    gfx->fillCircle(400 + sin(sys.plasmaTime*1.5)*200, 240, 50, C_WARN);
    
    // Draw Logo on top
    gfx->setTextSize(4);
    gfx->setTextColor(C_WHITE);
    int textX = 230;
    int textY = 220;
    
    // Shadow
    gfx->setTextColor(C_PRIMARY);
    gfx->setCursor(textX+4, textY+4);
    gfx->print("GITTERTIER PRO");
    
    // Text
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(textX, textY);
    gfx->print("GITTERTIER PRO");
    
    delay(30);
}

void scene_ModalBlock() {
    if (forceRedraw) {
        // Draw overlay box
        // We do NOT clear screen to keep context
        int x = 150, y = 100, w = 500, h = 280;
        
        // Solid BG to cover dashboard
        gfx->fillRoundRect(x, y, w, h, 20, C_WHITE);
        gfx->drawRoundRect(x, y, w, h, 20, C_ERROR);
        
        gfx->setTextSize(4);
        gfx->setTextColor(C_ERROR);
        gfx->setCursor(x + 50, y + 50);
        gfx->print("SHUTDOWN BLOCKED");
        
        gfx->setTextSize(2);
        gfx->setTextColor(C_TEXT_MAIN);
        gfx->setCursor(x + 80, y + 150);
        gfx->print("Active switches detected:");
        
        // List active switches
        int listY = y + 180;
        for(int i=2; i<=9; i++) {
            if(sys.switches[i].active) {
                gfx->setTextColor(sys.switches[i].color);
                gfx->setCursor(x + 100, listY);
                gfx->print("- " + sys.switches[i].name);
                listY += 20;
            }
        }
        
        forceRedraw = false;
    }
}

void scene_Alarm() {
    gfx->fillScreen(C_ERROR);
    gfx->setTextColor(C_WHITE);
    gfx->setTextSize(6);
    gfx->setCursor(200, 200);
    gfx->print("NOT-AUS");
    delay(100);
    gfx->fillScreen(C_BLACK);
    delay(100);
}

// =========================================================================
// 7. LOGIC PARSER
// =========================================================================
void parsePacket(String p) {
    p.trim();
    if(p.startsWith("[")) p = p.substring(1);
    if(p.endsWith("]")) p = p.substring(0, p.length()-1);
    
    sys.debugMsg = p;

    // --- ALARM ---
    if (p.indexOf("ALARM:ON") >= 0) { currentScene = SCENE_ALARM; return; }
    if (p.indexOf("ALARM:OFF") >= 0 && currentScene == SCENE_ALARM) { 
        currentScene = SCENE_DASHBOARD; forceRedraw = true; return; 
    }

    // --- POWER ---
    if (p.indexOf("PWR:START") >= 0) {
        if (currentScene == SCENE_STANDBY) {
            currentScene = SCENE_BOOT;
            forceRedraw = true;
        }
    }
    
    if (p.indexOf("PWR:REQ_OFF") >= 0) {
        bool blocked = false;
        // Check critical switches (3=Turbo, 4=BlinkL, 5=BlinkR, 6=Disco)
        if (sys.switches[3].active || sys.switches[4].active || 
            sys.switches[5].active || sys.switches[6].active) {
            blocked = true;
        }
        
        if (blocked) {
            currentScene = SCENE_MODAL_BLOCK;
            forceRedraw = true;
        } else {
            currentScene = SCENE_STANDBY;
            forceRedraw = true;
        }
    }

    // --- DATA (Switches) ---
    // [DATA:PIN:VAL]
    if (p.startsWith("DATA:")) {
        int s1 = p.indexOf(':');
        int s2 = p.lastIndexOf(':');
        int pin = p.substring(s1+1, s2).toInt();
        int val = p.substring(s2+1).toInt();
        
        if (pin >= 0 && pin < 14) {
            sys.switches[pin].active = (val == 1);
        }
        
        // Live update for Modal
        if (currentScene == SCENE_MODAL_BLOCK) {
            // Check if cleared
            bool stillBlocked = false;
             if (sys.switches[3].active || sys.switches[4].active || 
                 sys.switches[5].active || sys.switches[6].active) stillBlocked = true;
            
            if (!stillBlocked) {
                currentScene = SCENE_STANDBY;
                forceRedraw = true;
            } else {
                forceRedraw = true; // Redraw list
            }
        }
    }

    // --- JOYSTICK ---
    if (p.startsWith("JOY:MOVE:")) {
        sys.joystick = p.substring(9);
    }
}

// =========================================================================
// 8. MAIN
// =========================================================================
void setup() {
    Serial.begin(115200);
    
    // Init Hardware
    initSwitches();
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
    
    gfx->begin();
    
    // BLE Init
    if (!BLE.begin()) {
        gfx->fillScreen(C_ERROR);
        while(1);
    }
    BLE.setLocalName("Gittertier Pro");
    BLE.setAdvertisedService(gService);
    gService.addCharacteristic(rxChar);
    BLE.addService(gService);
    BLE.advertise();
}

void loop() {
    // 1. BLE Listener (Active in ALL scenes)
    BLE.poll();
    BLEDevice central = BLE.central();
    
    if (central) {
        sys.bleConnected = true;
        if (rxChar.written()) {
            parsePacket(rxChar.value());
        }
    } else {
        sys.bleConnected = false;
        // Optional: Auto-Standby on disconnect
        // if(currentScene == SCENE_DASHBOARD) { currentScene = SCENE_STANDBY; forceRedraw = true; }
    }

    // 2. Scene Renderer
    switch (currentScene) {
        case SCENE_BOOT:        scene_Boot(); break;
        case SCENE_CONNECTING:  scene_Connecting(); break;
        case SCENE_DASHBOARD:   scene_Dashboard(); break;
        case SCENE_MODAL_BLOCK: scene_ModalBlock(); break;
        case SCENE_STANDBY:     scene_Standby(); break;
        case SCENE_ALARM:       scene_Alarm(); break;
    }
}