#include "touch.h"
#include "config.h"
#include <Wire.h>

static unsigned long lastGestureTime = 0;
static const unsigned long DEBOUNCE_MS = 300;
static uint8_t lastGestureCode = 0;
static bool wasFingerDown = false;

static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

void touchInit() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(100000);

    // Hard reset
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(50);
    digitalWrite(TOUCH_RST, HIGH);
    delay(150);

    pinMode(TOUCH_INT, INPUT);

    uint8_t chipId = readReg(0x15);
    Serial.printf("[TOUCH] CST816S chip=0x%02X\n", chipId);

    // Disable auto-sleep so gestures are always detected
    writeReg(0xFE, 0x01);   // DisAutoSleep = 1
    writeReg(0xFA, 0x00);   // AutoSleepTime = 0

    // Enable gesture detection: double-click + continuous
    writeReg(0xEC, 0x05);   // MotionMask: EnDClick | EnConLR (continuous left-right)

    // Interrupt: detect on gesture
    writeReg(0xED, 0x01);   // IrqCtl: EnMotion

    Serial.println("[TOUCH] Configured OK");
}

TouchGesture touchPoll() {
    unsigned long now = millis();
    if (now - lastGestureTime < DEBOUNCE_MS) return GESTURE_NONE;

    uint8_t g = readReg(0x01);
    uint8_t fingers = readReg(0x02);

    // Detect new finger touch → allow next gesture
    bool fingerNow = (fingers > 0);
    if (fingerNow && !wasFingerDown) {
        lastGestureCode = 0;
    }
    wasFingerDown = fingerNow;

    // Gesture register cleared → reset tracking
    if (g == 0x00) {
        lastGestureCode = 0;
        return GESTURE_NONE;
    }
    if (g == 0xFF) return GESTURE_NONE;

    // Fire only once per gesture (register holds value until next touch)
    if (g == lastGestureCode) return GESTURE_NONE;

    lastGestureCode = g;
    lastGestureTime = now;
    Serial.printf("[TOUCH] gesture=0x%02X fingers=%d\n", g, fingers);
    return (TouchGesture)g;
}
