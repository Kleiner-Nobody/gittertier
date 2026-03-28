/* Hoverboard Dual Motor Serial Control + Speed Read + PWM Ramp
 *
 * Steuerung von LEFT und RIGHT Motor separat über Serial Commands:
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
 * Misst Geschwindigkeit beider Motoren separat
 * Sanftes Hochfahren der PWM (Ramp-Up) ohne Blockierung
 *
 * Platform: Arduino Nano
 */
 
#include <Arduino.h>
 
// CONSTANTS
const unsigned long SPEED_TIMEOUT = 500000;12
const unsigned int UPDATE_TIME = 500;
const double BAUD_RATE = 115200;
const double WHEEL_DIAMETER_CM = 17.0;
const double WHEEL_CIRCUMFERENCE_CM = WHEEL_DIAMETER_CM * 3.14159265;
 
// RAMP SETTINGS & HARDWARE CONFIG
const int RAMP_STEP = 1;      // Schrittweite PWM pro Intervall (kleiner = sanfter)
const int RAMP_DELAY = 15;    // Zeit zwischen Schritten in ms (15ms ist sehr weich)
unsigned long lastRampTime = 0; // Für non-blocking Ramp-Up

const bool INVERT_RIGHT_MOTOR = true; // Invertiert die logische Richtung für den rechten Motor
 
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
 
    Serial.begin(BAUD_RATE);
    Serial.println("Dual Motor Control Ready (Nano) with Non-Blocking PWM Ramp");
}
 
// MAIN LOOP
void loop()
{
    if (ReadFromSerial())
        ProcessCommand(_command, _data);
 
    // PWM Ramp-Up
    UpdatePWMRamp();
 
    // Geschwindigkeit messen
    ReadSpeedRight();
    ReadSpeedLeft();
 
    // Seriellausgabe
    WriteSpeedToSerial();
}
 
// READ SERIAL
bool ReadFromSerial()
{
    static String cmdBuffer;
    static String dataBuffer;
    static bool isCommand = true;
 
    if (!Serial.available()) return false;
 
    char recByte = Serial.read();
 
    if (recByte == '\r')
    {
        cmdBuffer.toUpperCase();
        _command = cmdBuffer;
        _data = dataBuffer.toInt();
 
        Serial.print("Received: ");
        Serial.print(_command);
        Serial.print(",");
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
 
    // Prüfen ob Befehl mit L oder R anfängt
    if (command.startsWith("L") || command.startsWith("R"))
    {
        char side = command.charAt(0);
        String cmd = command.substring(1);
        ExecuteCommand(cmd, data, side);
    }
    else
    {
        // Kein Präfix -> beide Motoren
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
        *pwmTarget = data; // Ramp-Up kümmert sich um den Rest
        
        // Wenn Richtung noch nicht gesetzt, default auf vorwärts (inkl. Invertierungs-Check für R)
        static bool dirSetL = false, dirSetR = false;
        if (side=='L' && !dirSetL) { digitalWrite(*pinDIR,1); dirSetL=true; }
        if (side=='R' && !dirSetR) { 
            int defaultDir = INVERT_RIGHT_MOTOR ? 0 : 1;
            digitalWrite(*pinDIR, defaultDir); 
            dirSetR=true; 
        }
 
        Serial.print(side);
        Serial.print(" PWM target = ");
        Serial.println(data);
    }
    else if (cmd == "BRAKE")
    {
        digitalWrite(*pinBRAKE, data);
        Serial.print(side);
        Serial.print(" BRAKE = ");
        Serial.println(data);
    }
    else if (cmd == "DIR")
    {
        int actualDirection = data;
        
        // Logische Invertierung für den rechten Motor, falls aktiviert
        if (side == 'R' && INVERT_RIGHT_MOTOR) {
            actualDirection = (data == 1) ? 0 : 1;
        }
        
        digitalWrite(*pinDIR, actualDirection);
        Serial.print(side);
        Serial.print(" DIR = ");
        Serial.println(data); // Konsolenausgabe zeigt den logischen Wert (z.B. "1" für vorwärts)
    }
    else
        Serial.println("Unknown command");
}
 
 
// PWM Ramp-Up Update (NON-BLOCKING)
void UpdatePWMRamp()
{
    unsigned long currentMillis = millis();
    
    // Nur aktualisieren, wenn RAMP_DELAY Millisekunden vergangen sind
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
        freqR = (1.0 / period) * 1000000.0; // Syntaxfehler aus Originalcode behoben
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
 
// WRITE SPEED TO SERIAL
void WriteSpeedToSerial()
{
    static unsigned long nextUpdate = 0;
    if (millis() < nextUpdate) return;
 
    Serial.print("LEFT  -> RPM:");
    Serial.print(rpmL,1);
    Serial.print(" KPH:");
    Serial.print(kphL,1);
 
    Serial.print("   RIGHT -> RPM:");
    Serial.print(rpmR,1);
    Serial.print(" KPH:");
    Serial.println(kphR,1);
 
    nextUpdate = millis() + UPDATE_TIME;
}