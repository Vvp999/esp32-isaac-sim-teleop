/*
  ESP32 joystick + LCD1602 robot eyes
  Board: Freenove ESP32-WROVER  (Arduino IDE: "ESP32 Wrover Module")
  Library: "LiquidCrystal I2C" by Frank de Brabander (Library Manager)

  Joystick:  VRX->32  VRY->33  SW->13  +5V->3.3V  GND->GND
  LCD (I2C): SDA->14  SCL->15  VCC->5V  GND->GND

  Serial OUT (unchanged, for joystick_bridge.py):  J,<x>,<y>,<button>
  Serial IN  (optional, from ROS later):           M,<MOOD>,<ms>
      MOOD = NORMAL | HAPPY | ANGRY | TIRED | SLEEP     e.g.  M,HAPPY,2000
*/
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- pins & settings ----------------
const int PIN_X = 32, PIN_Y = 33, PIN_BTN = 13;
const int I2C_SDA = 14, I2C_SCL = 15;

const float DEADBAND  = 0.08;
const int   PERIOD_MS = 20;          // 50 Hz
const int   LOOK_SIGN = 1;           // set to -1 if eyes look the wrong way
const unsigned long TIRED_MS = 8000, SLEEP_MS = 20000;

int centerX = 2048, centerY = 2048;
LiquidCrystal_I2C *lcd = nullptr;

// ---------------- eye graphics ----------------
// Each eye is 2x2 LCD cells. Per mood: L_TL, L_TR, R_TL, R_TR, BOT_L, BOT_R
enum Mood { NORMAL, BLINK, HAPPY, ANGRY, TIRED, SLEEP, MOOD_COUNT };

#define FULL 0b11111
const uint8_t BOT_L[8]  = {FULL, FULL, FULL, FULL, FULL, FULL, 0b01111, 0b00111};
const uint8_t BOT_R[8]  = {FULL, FULL, FULL, FULL, FULL, FULL, 0b11110, 0b11100};
const uint8_t EMPTY[8]  = {0, 0, 0, 0, 0, 0, 0, 0};

const uint8_t N_TL[8]   = {0b00111, 0b01111, FULL, FULL, FULL, FULL, FULL, FULL};
const uint8_t N_TR[8]   = {0b11100, 0b11110, FULL, FULL, FULL, FULL, FULL, FULL};

const uint8_t LID_T[8]  = {0, 0, 0, 0, 0, 0, 0, FULL};      // closed eye, upper half
const uint8_t LID_B[8]  = {FULL, 0, 0, 0, 0, 0, 0, 0};      // closed eye, lower half

const uint8_t H_TL[8]   = {0, 0, 0, 0b00011, 0b00111, 0b01110, 0b11100, 0b11000};  // ^
const uint8_t H_TR[8]   = {0, 0, 0, 0b11000, 0b11100, 0b01110, 0b00111, 0b00011};

const uint8_t A_LTL[8]  = {0b10000, 0b11000, 0b11100, 0b11110, FULL, FULL, FULL, FULL};  // brows slope inward
const uint8_t A_LTR[8]  = {0, 0, 0, 0, 0b10000, 0b11000, 0b11100, 0b11110};
const uint8_t A_RTL[8]  = {0, 0, 0, 0, 0b00001, 0b00011, 0b00111, 0b01111};
const uint8_t A_RTR[8]  = {0b00001, 0b00011, 0b00111, 0b01111, FULL, FULL, FULL, FULL};

const uint8_t T_T[8]    = {0, 0, 0, 0, FULL, FULL, FULL, FULL};  // half-closed lids

const uint8_t *GLYPHS[MOOD_COUNT][6] = {
  /* NORMAL */ {N_TL,  N_TR,  N_TL,  N_TR,  BOT_L, BOT_R},
  /* BLINK  */ {LID_T, LID_T, LID_T, LID_T, LID_B, LID_B},
  /* HAPPY  */ {H_TL,  H_TR,  H_TL,  H_TR,  EMPTY, EMPTY},
  /* ANGRY  */ {A_LTL, A_LTR, A_RTL, A_RTR, BOT_L, BOT_R},
  /* TIRED  */ {T_T,   T_T,   T_T,   T_T,   BOT_L, BOT_R},
  /* SLEEP  */ {LID_T, LID_T, LID_T, LID_T, LID_B, LID_B},
};

int loadedMood = -1, shownMood = -1, shownLook = 99;

void drawEyes(int mood, int look) {
  if (!lcd || (mood == shownMood && look == shownLook)) return;
  if (mood != loadedMood) {                       // upload this mood's glyphs (slots 1..6)
    for (int i = 0; i < 6; i++) lcd->createChar(i + 1, (uint8_t *)GLYPHS[mood][i]);
    loadedMood = mood;
  }
  int lx = 4 + look * 2, rc = 10 + look * 2;      // eye columns
  for (int row = 0; row < 2; row++) {
    lcd->setCursor(0, row);
    for (int col = 0; col < 16; col++) {
      uint8_t c = ' ';
      if (row == 0) {
        if (col == lx) c = 1; else if (col == lx + 1) c = 2;
        else if (col == rc) c = 3; else if (col == rc + 1) c = 4;
        else if (mood == SLEEP && col == 14) c = 'z';
      } else {
        if (col == lx || col == rc) c = 5; else if (col == lx + 1 || col == rc + 1) c = 6;
      }
      lcd->write(c);
    }
  }
  shownMood = mood; shownLook = look;
}

// ---------------- joystick ----------------
int readAvg(int pin) {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(pin);
  return sum / 8;
}

float normalize(int raw, int center) {
  float v = (raw >= center) ? (float)(raw - center) / (4095 - center)
                            : (float)(raw - center) / center;
  if (fabs(v) < DEADBAND) return 0.0;
  v = (v > 0) ? (v - DEADBAND) / (1 - DEADBAND) : (v + DEADBAND) / (1 - DEADBAND);
  return constrain(v, -1.0, 1.0);
}

// ---------------- mood override from serial (for ROS later) ----------------
int overrideMood = NORMAL;
unsigned long overrideUntil = 0;
String cmdBuf;

void readSerialCommands() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      cmdBuf.trim();
      if (cmdBuf.startsWith("M,")) {
        int c2 = cmdBuf.indexOf(',', 2);
        String name = (c2 > 0) ? cmdBuf.substring(2, c2) : cmdBuf.substring(2);
        unsigned long dur = (c2 > 0) ? cmdBuf.substring(c2 + 1).toInt() : 2000;
        name.toUpperCase();
        int m = -1;
        if (name == "NORMAL") m = NORMAL; else if (name == "HAPPY") m = HAPPY;
        else if (name == "ANGRY") m = ANGRY; else if (name == "TIRED") m = TIRED;
        else if (name == "SLEEP") m = SLEEP;
        if (m >= 0) { overrideMood = m; overrideUntil = millis() + dur; }
      }
      cmdBuf = "";
    } else if (cmdBuf.length() < 40) {
      cmdBuf += ch;
    }
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_BTN, INPUT_PULLUP);
  randomSeed(esp_random());

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);                       // fast I2C so redraws don't slow the joystick
  const uint8_t addrs[] = {0x27, 0x3F};
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { lcd = new LiquidCrystal_I2C(a, 16, 2); break; }
  }
  if (lcd) {
    lcd->init();
    lcd->backlight();
    lcd->setCursor(0, 0); lcd->print("  Calibrating   ");
    lcd->setCursor(0, 1); lcd->print(" don't touch :) ");
  } else {
    Serial.println("# LCD not found (check SDA=14, SCL=15, 5V, GND)");
  }

  // calibrate joystick center: keep it untouched for ~1 s after reset
  delay(300);
  long sx = 0, sy = 0;
  for (int i = 0; i < 50; i++) { sx += analogRead(PIN_X); sy += analogRead(PIN_Y); delay(10); }
  centerX = sx / 50; centerY = sy / 50;
  Serial.printf("# calibrated center x=%d y=%d\n", centerX, centerY);
}

void loop() {
  static unsigned long last = 0, lastActive = 0, nextBlink = 0, blinkUntil = 0;
  static bool wasSleeping = false;

  readSerialCommands();

  unsigned long now = millis();
  if (now - last < PERIOD_MS) return;
  last = now;

  float x = normalize(readAvg(PIN_X), centerX);
  float y = normalize(readAvg(PIN_Y), centerY);
  int btn = (digitalRead(PIN_BTN) == LOW) ? 1 : 0;
  Serial.printf("J,%.3f,%.3f,%d\n", x, y, btn);

  // ----- decide what the eyes do -----
  bool active = (x != 0.0 || y != 0.0 || btn);
  if (active) lastActive = now;
  unsigned long idle = now - lastActive;

  int look = (x * LOOK_SIGN < -0.3) ? -1 : (x * LOOK_SIGN > 0.3) ? 1 : 0;
  int mood;

  if (wasSleeping && active) {                 // woken up -> happy for 1.5 s
    wasSleeping = false;
    overrideMood = HAPPY; overrideUntil = now + 1500;
  }

  if (now < overrideUntil)      mood = overrideMood;
  else if (active)              mood = btn ? ANGRY : NORMAL;
  else if (idle > SLEEP_MS)   { mood = SLEEP; wasSleeping = true; look = 0; }
  else if (idle > TIRED_MS)     mood = TIRED;
  else                          mood = NORMAL;

  if (mood == NORMAL) {                        // random blinking
    if (now > nextBlink) { blinkUntil = now + 150; nextBlink = now + random(2000, 5000); }
    if (now < blinkUntil) mood = BLINK;
  }

  drawEyes(mood, look);
}
