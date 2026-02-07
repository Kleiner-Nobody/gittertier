#include <Arduino_GFX_Library.h>
#include <ArduinoBLE.h>

#define GFX_BL 2
// 24-Parameter Konstruktor für volle Kompatibilität
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    GFX_NOT_DEFINED, GFX_NOT_DEFINED, GFX_NOT_DEFINED,
    40, 41, 39, 42, 45, 48, 47, 21, 14, 5, 6, 7, 15, 16, 4, 8, 3, 46, 9, 1, false
);
Arduino_RPi_DPI_RGBPanel *gfx = new Arduino_RPi_DPI_RGBPanel(bus, 800, 0, 8, 4, 8, 480, 0, 8, 4, 8, 1, 16000000, true);

// Design System
#define C_GITTER_BG 0xF7BE
#define C_WHITE     0xFFFF
#define C_BLUE      0x03DF
#define C_TEXT      0x2104
#define C_ALARM     0xF800

struct UIRegion { int x, y, w, h; String last; };
UIRegion regMain = {50, 100, 700, 240, ""};
UIRegion regInfo = {50, 360, 700, 90, ""};

// Echte Projektdaten
String facts[] = {
  "ZEITAUFWAND: 208:26:19 STUNDEN",
  "GESAMTBUDGET: 321,47 EURO",
  "PROTOKOLL: 99 EINTRÄGE",
  "HISTORY: SYLVAN GOLDMAN (1937)",
  "MODEL: KLAPPSTUHL-BASIS"
};

bool authenticated = false;
int factIdx = 0;

BLEService gService("19b10000-e8f2-537e-4f6c-d104768a1214");
BLEStringCharacteristic rxChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLERead | BLEWrite, 512);

void setup() {
  pinMode(GFX_BL, OUTPUT); digitalWrite(GFX_BL, HIGH);
  gfx->begin();
  if (!BLE.begin()) while(1);
  
  BLE.setLocalName("Gittertier Pro");
  BLE.setAdvertisedService(gService);
  gService.addCharacteristic(rxChar);
  BLE.addService(gService);
  BLE.advertise();
}

void loop() {
  BLE.poll();

  // Watchdog: Bei Verbindungsverlust Reset
  if (authenticated && !BLE.central().connected()) {
    authenticated = false;
    gfx->fillScreen(C_GITTER_BG); // Auto-Restart UI
  }

  if (rxChar.written()) {
    String val = rxChar.value();
    if (val == "GITTER_77_PRO_SEC") {
      authenticated = true;
      drawStaticUI();
    }
    handleCommands(val);
  }

  if (!authenticated) {
    drawSearchUI();
  } else {
    updateDashboard();
  }
}

void handleCommands(String c) {
  if (c == "BOX|STOP") {
    gfx->fillScreen(C_ALARM);
    drawCentered(220, "!!! NOT-AUS !!!", 5, C_WHITE);
  } else if (c == "BOX|RES") {
    authenticated = false; // Triggered Re-Auth
  }
}

void drawStaticUI() {
  gfx->fillScreen(C_GITTER_BG);
  gfx->fillRect(0, 0, 800, 70, C_WHITE);
  gfx->fillRoundRect(50, 100, 700, 240, 20, C_WHITE);
  gfx->fillRoundRect(50, 360, 700, 90, 15, C_WHITE);
  drawText(30, 25, "GITTERTIER PRO | DASHBOARD", 3, C_BLUE);
}

void updateDashboard() {
  static unsigned long lastF = 0;
  if (millis() - lastF > 6000) {
    factIdx = (factIdx + 1) % 5;
    lastF = millis();
  }
  smartUpdate(regInfo, facts[factIdx], 2, C_TEXT, C_WHITE, true);
  smartUpdate(regMain, "SYSTEM ONLINE\nHARDWARE VERIFIZIERT", 3, C_BLUE, C_WHITE, true);
}

void drawSearchUI() {
  static int a = 0;
  gfx->fillScreen(C_GITTER_BG);
  drawCentered(180, "WARTE AUF KASTEN...", 3, C_TEXT);
  drawCentered(230, "ID: GITTER_77_PRO_SEC", 2, 0x8C71);
  // Animierter Circle
  gfx->drawCircle(400, 350, (a%100), C_BLUE);
  a+=2;
  delay(20);
}

void smartUpdate(UIRegion &r, String n, int s, uint16_t c, uint16_t bg, bool cent) {
  if (r.last == n) return;
  gfx->fillRect(r.x, r.y, r.w, r.h, bg);
  gfx->setTextSize(s); gfx->setTextColor(c);
  if (cent) {
    int tw = n.length() * s * 6;
    gfx->setCursor(r.x + (r.w - tw)/2, r.y + r.h/2 - 8);
  } else {
    gfx->setCursor(r.x + 15, r.y + 15);
  }
  gfx->print(n);
  r.last = n;
}

void drawCentered(int y, String t, int s, uint16_t c) {
  gfx->setTextSize(s); gfx->setTextColor(c);
  gfx->setCursor(400 - (t.length()*s*3), y);
  gfx->print(t);
}

void drawText(int x, int y, String t, int s, uint16_t c) {
  gfx->setTextSize(s); gfx->setTextColor(c);
  gfx->setCursor(x, y); gfx->print(t);
}