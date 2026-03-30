/* Hoverboard Dual Motor Serial Control + Speed Read + PWM Ramp
 *
 * Steuerung von LEFT und RIGHT Motor separat über Serial Commands:
 * Empfängt Daten nun sicher über SoftwareSerial (Pin 8) vom R4 (38400 Baud)
 * Debug-Ausgaben an PC über Hardware Serial (USB) (115200 Baud)
 *
 * LPWM,120   -> nur linker Motor
 * RPWM,200   -> nur rechter Motor
 * PWM,150    -> beide Motoren gleichzeitig
 * LDIR,1     -> links vorwärts
 * RDIR,0     -> rechts rückwärts
 * DIR,1      -> beide Motoren vorwärts
 * LBRAKE,0   -> linker Motor Bremse aus
 * BRAKE,1    -> beide Motoren bremsen
 *
 * Platform: Arduino Nano
 */
 
#include <Arduino.h>
#include <SoftwareSerial.h> // NEU: Für die konfliktfreie Kommunikation mit dem R4
 
// CONSTANTS
const unsigned long SPEED_TIMEOUT = 500000;
const unsigned int UPDATE_TIME = 500;
const double WHEEL_DIAMETER_CM = 17.0;
const double WHEEL_CIRCUMFERENCE_CM = WHEEL_DIAMETER_CM * 3.14159265;
 
// RAMP SETTINGS & HARDWARE CONFIG
const int RAMP_STEP = 1;      
const int RAMP_DELAY = 15;    
unsigned long lastRampTime = 0; 

const bool INVERT_RIGHT_MOTOR = true; 

// KOMMUNIKATIONS PINS (SoftwareSerial)
const int PIN_R4_RX = 8; // Hier kommt das Kabel vom R4 TX (Pin 8) an!
const int PIN_R4_TX = 7; // Wird nicht physisch benötigt, aber für Bibliothek erforderlich
SoftwareSerial r4Serial(PIN_R4_RX, PIN_R4_TX);
 
// MOTOR PINS
const int PIN_DIR_R   = 2;
const int PIN_BRAKE_R = 3;
const int PIN_PWM_R   = 9;
const int PIN_SPEED_R = 12;
 
const int PIN_DIR_L   = 4;
const int PIN_BRAKE_L = 5;
const int PIN_PWM_L   = 10;
const int PIN_SPEED_L = 11;
 
// SERIAL VARIABLES
String _command = "";
int _data = 0;
 
// SPEED VARIABLES RIGHT
double freqR = 0, rpmR = 0, mphR = 0, kphR = 0;
 
// SPEED VARIABLES LEFT
double freqL = 0, rpmL = 0, mphL = 0, kphL = 0;
 
// CURRENT PWM VALUES (für Ramp-Up)
int pwmTargetL = 0, pwmCurrentL = 0;
int pwmTargetR = 0, pwmCurrentR = 0;
 
// SETUP
void setup()
{
    // PC Debugging (USB)
    Serial.begin(115200);
    
    // R4 Kommunikation (Pin 8)
    r4Serial.begin(38400); 

    // RIGHT MOTOR
    pinMode(PIN_SPEED_R, INPUT);
    pinMode(PIN_PWM_R, OUTPUT);
    pinMode(PIN_BRAKE_R, OUTPUT);
    pinMode(PIN_DIR_R, OUTPUT);
    digitalWrite(PIN_BRAKE_R, LOW);
    digitalWrite(PIN_DIR_R, LOW);
    analogWrite(PIN_PWM_R, 0);
 
    // LEFT MOTOR
    pinMode(PIN_SPEED_L, INPUT);
    pinMode(PIN_PWM_L, OUTPUT);
    pinMode(PIN_BRAKE_L, OUTPUT);
    pinMode(PIN_DIR_L, OUTPUT);
    digitalWrite(PIN_BRAKE_L, LOW);
    digitalWrite(PIN_DIR_L, LOW);
    analogWrite(PIN_PWM_L, 0);
 
    Serial.println("==========================================");
    Serial.println("MOTOR NANO ONLINE & BEREIT");
    Serial.println("Warte auf Befehle von R4 auf PIN 8...");
    Serial.println("==========================================");
}
 
// MAIN LOOP
void loop()
{
    // 1. Lauschen auf den R4 (Pin 8)
    if (ReadFromR4()) {
        ProcessCommand(_command, _data);
    }
 
    // 2. PWM Ramp-Up
    UpdatePWMRamp();
 
    // 3. Geschwindigkeit messen
    ReadSpeedRight();
    ReadSpeedLeft();
 
    // 4. Seriellausgabe (Nur wenn Bewegung da ist, um Spam zu vermeiden)
    WriteSpeedToSerial();
}
 
// READ FROM R4 (SoftwareSerial)
bool ReadFromR4()
{
    static String cmdBuffer;
    static String dataBuffer;
    static bool isCommand = true;
 
    if (!r4Serial.available()) return false;
 
    char recByte = r4Serial.read();
 
    if (recByte == '\r')
    {
        cmdBuffer.toUpperCase();
        _command = cmdBuffer;
        _data = dataBuffer.toInt();
 
        // DEBUG AUSGABE FÜR DICH: Wir zeigen exakt, was reinkam
        Serial.print("\n[R4 EMPFANGEN] -> Befehl: ");
        Serial.print(_command);
        Serial.print(" | Wert: ");
        Serial.println(_data);
 
        cmdBuffer = "";
        dataBuffer = "";
        isCommand = true;
 
        return true;
    }
 
    if (recByte == ',')
    {
        isCommand = false;
        return false;
    }
 
    if (isCommand)
        cmdBuffer += recByte;
    else
        dataBuffer += recByte;
 
    return false;
}
 
// PROCESS COMMAND
void ProcessCommand(String command, int data)
{
    command.toUpperCase();
 
    if (command.startsWith("L") || command.startsWith("R"))
    {
        char side = command.charAt(0);
        String cmd = command.substring(1);
        ExecuteCommand(cmd, data, side);
    }
    else
    {
        ExecuteCommand(command, data, 'L');
        ExecuteCommand(command, data, 'R');
    }
}
 
// EXECUTE SINGLE COMMAND
void ExecuteCommand(String cmd, int data, char side)
{
    int *pinPWM, *pinBRAKE, *pinDIR;
    int *pwmTarget;
 
    if (side == 'L') { pinPWM = &PIN_PWM_L; pinBRAKE = &PIN_BRAKE_L; pinDIR = &PIN_DIR_L; pwmTarget = &pwmTargetL; }
    else { pinPWM = &PIN_PWM_R; pinBRAKE = &PIN_BRAKE_R; pinDIR = &PIN_DIR_R; pwmTarget = &pwmTargetR; }
 
    if (cmd == "PWM")
    {
        data = constrain(data, 0, 255);
        *pwmTarget = data; 
        
        static bool dirSetL = false, dirSetR = false;
        if (side=='L' && !dirSetL) { digitalWrite(*pinDIR,1); dirSetL=true; }
        if (side=='R' && !dirSetR) { 
            int defaultDir = INVERT_RIGHT_MOTOR ? 0 : 1;
            digitalWrite(*pinDIR, defaultDir); 
            dirSetR=true; 
        }
 
        Serial.print("   -> Setze "); Serial.print(side);
        Serial.print(" PWM ZIEL auf: "); Serial.println(data);
    }
    else if (cmd == "BRAKE")
    {
        digitalWrite(*pinBRAKE, data);
        Serial.print("   -> Setze "); Serial.print(side);
        Serial.print(" BREMSE auf: "); Serial.println(data);
    }
    else if (cmd == "DIR")
    {
        int actualDirection = data;
        
        if (side == 'R' && INVERT_RIGHT_MOTOR) {
            actualDirection = (data == 1) ? 0 : 1;
        }
        
        digitalWrite(*pinDIR, actualDirection);
        Serial.print("   -> Setze "); Serial.print(side);
        Serial.print(" RICHTUNG auf: "); Serial.println(data);
    }
    else {
        Serial.println("   -> [FEHLER] Unbekannter Befehl ignoriert!");
    }
}
 
// PWM Ramp-Up Update (NON-BLOCKING)
void UpdatePWMRamp()
{
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastRampTime >= RAMP_DELAY) 
    {
        lastRampTime = currentMillis;

        // LEFT
        if (pwmCurrentL < pwmTargetL) { 
            pwmCurrentL += RAMP_STEP; 
            if (pwmCurrentL > pwmTargetL) pwmCurrentL = pwmTargetL; 
        }
        else if (pwmCurrentL > pwmTargetL) { 
            pwmCurrentL -= RAMP_STEP; 
            if (pwmCurrentL < pwmTargetL) pwmCurrentL = pwmTargetL; 
        }
        analogWrite(PIN_PWM_L, pwmCurrentL);
     
        // RIGHT
        if (pwmCurrentR < pwmTargetR) { 
            pwmCurrentR += RAMP_STEP; 
            if (pwmCurrentR > pwmTargetR) pwmCurrentR = pwmTargetR; 
        }
        else if (pwmCurrentR > pwmTargetR) { 
            pwmCurrentR -= RAMP_STEP; 
            if (pwmCurrentR < pwmTargetR) pwmCurrentR = pwmTargetR; 
        }
        analogWrite(PIN_PWM_R, pwmCurrentR);
    }
}
 
// READ SPEED RIGHT
void ReadSpeedRight()
{
    static bool lastState = false;
    static unsigned long lastTime = 0;
    static unsigned long timeout = 0;
 
    bool state = digitalRead(PIN_SPEED_R);
    if (state != lastState)
    {
        unsigned long now = micros();
        double elapsed = now - lastTime;
        double period = elapsed * 2.0;
        freqR = (1.0 / period) * 1000000.0;
        rpmR = freqR / 45.0 * 60.0;
        if (rpmR > 5000) rpmR = 0;
        kphR = (WHEEL_CIRCUMFERENCE_CM * rpmR * 60.0) / 100000.0;
 
        lastTime = now;
        timeout = now + SPEED_TIMEOUT;
        lastState = state;
    }
    else if (micros() > timeout) freqR = rpmR = kphR = 0;
}
 
// READ SPEED LEFT
void ReadSpeedLeft()
{
    static bool lastState = false;
    static unsigned long lastTime = 0;
    static unsigned long timeout = 0;
 
    bool state = digitalRead(PIN_SPEED_L);
    if (state != lastState)
    {
        unsigned long now = micros();
        double elapsed = now - lastTime;
        double period = elapsed * 2.0;
        freqL = (1.0 / period) * 1000000.0;
        rpmL = freqL / 45.0 * 60.0;
        if (rpmL > 5000) rpmL = 0;
        kphL = (WHEEL_CIRCUMFERENCE_CM * rpmL * 60.0) / 100000.0;
 
        lastTime = now;
        timeout = now + SPEED_TIMEOUT;
        lastState = state;
    }
    else if (micros() > timeout) freqL = rpmL = kphL = 0;
}
 
// WRITE SPEED TO SERIAL (Reduziert auf echte Bewegungen)
void WriteSpeedToSerial()
{
    static unsigned long nextUpdate = 0;
    if (millis() < nextUpdate) return;
 
    // Drucke die Geschwindigkeit NUR, wenn der Roboter sich tatsächlich bewegt!
    if (rpmL > 1.0 || rpmR > 1.0) {
        Serial.print("[INFO] Bewegung: LEFT -> RPM:");
        Serial.print(rpmL,1);
        Serial.print(" | RIGHT -> RPM:");
        Serial.println(rpmR,1);
    }
 
    nextUpdate = millis() + UPDATE_TIME;
}