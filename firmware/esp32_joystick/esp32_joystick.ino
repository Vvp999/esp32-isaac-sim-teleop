/*
  ESP32 joystick -> serial -> ROS 2 /cmd_vel (via joystick_bridge.py)
  Board: Freenove ESP32-WROVER (Arduino IDE board: "ESP32 Wrover Module")

  Wiring (joystick module from the Freenove kit):
    Joystick GND -> ESP32 GND
    Joystick +5V -> ESP32 3.3V   (use 3.3V, NOT 5V: ESP32 pins are 3.3V only)
    Joystick VRX -> GPIO 32
    Joystick VRY -> GPIO 33
    Joystick SW  -> GPIO 13
  These pins are free even with the camera attached.

  Output, 50 times per second, one line:
    J,<x>,<y>,<button>
  x, y are -1.000 .. 1.000 (0 = centered), button is 1 when pressed.
*/

const int PIN_X   = 32;
const int PIN_Y   = 33;
const int PIN_BTN = 13;

const float DEADBAND = 0.08;     // ignore small wobble around the center
const int   PERIOD_MS = 20;      // 50 Hz

int centerX = 2048;
int centerY = 2048;

int readAvg(int pin) {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(pin);
  return sum / 8;
}

float normalize(int raw, int center) {
  float v;
  if (raw >= center) v = (float)(raw - center) / (4095 - center);
  else               v = (float)(raw - center) / center;
  if (fabs(v) < DEADBAND) return 0.0;
  // rescale so output starts at 0 right after the deadband
  v = (v > 0) ? (v - DEADBAND) / (1 - DEADBAND) : (v + DEADBAND) / (1 - DEADBAND);
  return constrain(v, -1.0, 1.0);
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);                 // 0..4095
  analogSetAttenuation(ADC_11db);           // full 0..3.3V range
  pinMode(PIN_BTN, INPUT_PULLUP);

  // Calibrate: keep the joystick untouched for 1 second after reset
  delay(300);
  long sx = 0, sy = 0;
  for (int i = 0; i < 50; i++) { sx += analogRead(PIN_X); sy += analogRead(PIN_Y); delay(10); }
  centerX = sx / 50;
  centerY = sy / 50;
  Serial.printf("# calibrated center x=%d y=%d\n", centerX, centerY);
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last < PERIOD_MS) return;
  last = millis();

  float x = normalize(readAvg(PIN_X), centerX);
  float y = normalize(readAvg(PIN_Y), centerY);
  int btn = (digitalRead(PIN_BTN) == LOW) ? 1 : 0;

  Serial.printf("J,%.3f,%.3f,%d\n", x, y, btn);
}
