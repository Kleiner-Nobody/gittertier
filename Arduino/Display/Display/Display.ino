#include <Arduino_GFX_Library.h>
#include <ArduinoBLE.h>

/* =========================================================================
   1. HARDWARE & DISPLAY SETUP
   ========================================================================= */

#define GFX_BL 2

// Display Treiber (JC8048W550 / ESP32-S3)
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    GFX_NOT_DEFINED, GFX_NOT_DEFINED, GFX_NOT_DEFINED,
    40, 41, 39, 42,
    45, 48, 47, 21, 14,
    5, 6, 7, 15, 16, 4,
    8, 3, 46, 9, 1
);
Arduino_RPi_DPI_RGBPanel *gfx = new Arduino_RPi_DPI_RGBPanel(bus, 800, 0, 8, 4, 8, 480, 0, 8, 4, 8, 1, 16000000, true);

/* =========================================================================
   2. UI KONFIGURATION
   ========================================================================= */

#define C_BG          0xF7BE // Hellgrau
#define C_WHITE       0xFFFF 
#define C_BLUE        0x03DF // Gittertier Blau
#define C_TEXT        0x0000
#define C_GREEN       0x0400 // Status Grün

// BLE UUIDs
#define SERVICE_UUID           "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHARACTERISTIC_UUID    "19b10001-e8f2-537e-4f6c-d104768a1214"

BLEService gittertierService(SERVICE_UUID);
BLEStringCharacteristic rxCharacteristic(CHARACTERISTIC_UUID, BLERead | BLEWrite, 512);

/* =========================================================================
   3. STATUS VARIABLEN (State Machine)
   ========================================================================= */

// Wir speichern den "alten" Zustand, um nur bei Änderungen zu zeichnen
int currentPoints = 0;
int lastPoints = -1;

float currentTotal = 0.00;
float lastTotal = -1.00;

String lastAction = "Bereit...";
String currentAction = "Bereit...";

bool deviceConnected = false;
bool wasConnected = false;

/* =========================================================================
   4. UI FUNKTIONEN (OPTIMIERT)
   ========================================================================= */

// Diese Funktion malt NUR den Hintergrund einer Zahl neu, bevor die neue kommt.
// Das ist 100x schneller als fillScreen().
void drawValueUpdate(int x, int y, int w, int h, String oldValue, String newValue, uint16_t color, int size) {
  if (oldValue.equals(newValue)) return; // Nichts zu tun
  
  // Alten Bereich löschen (Hintergrundfarbe malen)
  gfx->fillRect(x, y, w, h, C_WHITE);
  
  // Neuen Wert schreiben
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y + 10); // Leichtes Padding
  gfx->print(newValue);
}

void drawStaticInterface() {
  gfx->fillScreen(C_BG);
  
  // Weiße Karte in der Mitte
  gfx->fillRoundRect(50, 50, 700, 380, 20, C_WHITE);
  
  // Header Text
  gfx->setTextSize(3);
  gfx->setTextColor(C_TEXT);
  gfx->setCursor(80, 80);
  gfx->print("DEINE PUNKTE");
  
  // Footer Bereich
  gfx->drawFastHLine(50, 320, 700, 0xBDF7);
  
  // Label unten links
  gfx->setTextSize(2);
  gfx->setTextColor(0x8C71); // Grau
  gfx->setCursor(80, 350);
  gfx->print("Letzter Scan:");
  
  // Label unten rechts
  gfx->setCursor(450, 350);
  gfx->print("Einkaufswert:");
}

void updateDisplay() {
  // 1. UPDATE PUNKTE (Riesig in der Mitte)
  if (currentPoints != lastPoints) {
    // Bereich löschen: x=80, y=140, w=600, h=100
    gfx->fillRect(80, 140, 600, 120, C_WHITE);
    
    gfx->setTextSize(12); // EXTREM GROSS
    gfx->setTextColor(C_BLUE);
    gfx->setCursor(80, 150);
    gfx->print(currentPoints);
    
    lastPoints = currentPoints;
  }
  
  // 2. UPDATE PREIS (Unten Rechts)
  if (currentTotal != lastTotal) {
    gfx->fillRect(450, 380, 250, 40, C_WHITE);
    
    gfx->setTextSize(3);
    gfx->setTextColor(C_TEXT);
    gfx->setCursor(450, 385);
    gfx->print(String(currentTotal, 2) + " E");
    
    lastTotal = currentTotal;
  }
  
  // 3. UPDATE LETZTE AKTION (Unten Links)
  if (!currentAction.equals(lastAction)) {
    gfx->fillRect(80, 380, 350, 40, C_WHITE);
    
    gfx->setTextSize(2);
    gfx->setTextColor(C_TEXT);
    gfx->setCursor(80, 385);
    
    // Text kürzen falls zu lang
    String printText = currentAction;
    if (printText.length() > 25) printText = printText.substring(0, 22) + "...";
    gfx->print(printText);
    
    lastAction = currentAction;
  }
  
  // 4. CONNECTION ICON (Oben Rechts)
  if (deviceConnected != wasConnected) {
    uint16_t c = deviceConnected ? C_BLUE : 0x8C71;
    gfx->fillCircle(700, 90, 15, c);
    gfx->fillCircle(700, 90, 8, C_WHITE); // Ring Effekt
    wasConnected = deviceConnected;
  }
}

/* =========================================================================
   5. LOGIK & PARSING
   ========================================================================= */

void parseData(String data) {
  // Protokoll V2: "TYPE|VALUE|EXTRA"
  // Beispiele:
  // "ADD|Milch|1.20"  -> Fügt Produkt hinzu (Preis addieren)
  // "PTS|150"         -> Setzt Punkte direkt auf 150
  
  if (data.startsWith("ADD|")) {
    int firstPipe = data.indexOf('|');
    int secondPipe = data.lastIndexOf('|');
    
    String name = data.substring(firstPipe + 1, secondPipe);
    float price = data.substring(secondPipe + 1).toFloat();
    
    currentTotal += price;
    currentAction = name; // Zeige Name unten an
    
    // Simuliere Punktezuwachs (einfach +10 pro Item für Demo, 
    // bis echte Daten vom Web kommen)
    currentPoints += 10; 
  }
  else if (data.startsWith("PTS|")) {
    int pipe = data.indexOf('|');
    currentPoints = data.substring(pipe + 1).toInt();
  }
  else if (data.startsWith("CLEAR")) {
    currentTotal = 0;
    currentPoints = 0; // Oder behalten, je nach Logik
    currentAction = "Warenkorb geleert";
  }
  
  // Update sofort anfordern!
  updateDisplay();
}

/* =========================================================================
   6. SETUP & LOOP
   ========================================================================= */

void setup() {
  Serial.begin(115200);
  
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  
  gfx->begin();
  drawStaticInterface(); // Einmalig das Grundgerüst malen
  
  if (!BLE.begin()) {
    gfx->setCursor(10, 10);
    gfx->print("BLE FEHLER!");
    while(1);
  }
  
  BLE.setLocalName("Gittertier Pro");
  BLE.setAdvertisedService(gittertierService);
  gittertierService.addCharacteristic(rxCharacteristic);
  BLE.addService(gittertierService);
  BLE.advertise();
}

void loop() {
  BLEDevice central = BLE.central();
  
  if (central) {
    deviceConnected = true;
    updateDisplay(); // Icon update
    
    while (central.connected()) {
      if (rxCharacteristic.written()) {
        parseData(rxCharacteristic.value());
      }
    }
    
    deviceConnected = false;
    updateDisplay(); // Icon update
  }
}