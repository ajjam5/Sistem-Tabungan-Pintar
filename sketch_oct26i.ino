#include <Wire.h>
#include <EEPROM.h>

class MiniLCD : public Print {
  public:
    MiniLCD(uint8_t address) : addr(address), bl(0x08) {}

    void begin() {
      Wire.begin();
      delay(50);
      send4(0x30, 0); delay(5);
      send4(0x30, 0); delay(5);
      send4(0x30, 0); delay(1);
      send4(0x20, 0);
      command(0x28);
      command(0x0C);
      command(0x06);
      clear();
    }
    void clear()       { command(0x01); delay(2); }
    void display()     { command(0x0C); }
    void noDisplay()   { command(0x08); }
    void backlight()   { bl = 0x08; expander(0); }
    void noBacklight() { bl = 0x00; expander(0); }
    void setCursor(uint8_t col, uint8_t row) {
      command(0x80 | (col + (row ? 0x40 : 0x00)));
    }
    virtual size_t write(uint8_t c) { send(c, 1); return 1; }

  private:
    uint8_t addr, bl;
    void expander(uint8_t d) {
      Wire.beginTransmission(addr);
      Wire.write(d | bl);
      Wire.endTransmission();
    }
    void pulse(uint8_t d) {
      expander(d | 0x04);
      delayMicroseconds(2);
      expander(d & 0xFB);
      delayMicroseconds(60);
    }
    void send4(uint8_t n, uint8_t rs) { pulse((n & 0xF0) | rs); }
    void send(uint8_t v, uint8_t rs) {
      send4(v & 0xF0, rs);
      send4((uint8_t)(v << 4) & 0xF0, rs);
    }
    void command(uint8_t c) { send(c, 0); }
};

MiniLCD lcd(0x27);

const uint8_t PIN_BTN[7] = {2, 3, 4, 5, 6, 7, 8};
const uint8_t PIN_BUZZER = 9;

const unsigned long NOMINAL[5] = {5000UL, 10000UL, 20000UL, 50000UL, 100000UL};

const int EEPROM_ADDR = 0;
const unsigned long DEBOUNCE_MS   = 40;
const unsigned long RESET_HOLD_MS = 3000;
const unsigned long INTRO_MS      = 5000;    // total setelah tekan ON: 5 detik
const unsigned long RESULT_MS     = 10000;   // total baru setelah pilih nominal: 10 detik
const unsigned long AUTO_OFF_MS   = 60000;   // harus lebih besar dari RESULT_MS

enum State { ST_OFF, ST_INTRO, ST_MENU, ST_RESULT };
State state = ST_OFF;

unsigned long total = 0;

bool rawLast[7], stableNow[7], pressed[7];
unsigned long changeAt[7];

unsigned long stateSince = 0, lastActivity = 0, resetStart = 0;
bool resetDone = false;
int lastSec = -1;
void formatRp(unsigned long v, char *out) {
  char tmp[12];
  ultoa(v, tmp, 10);
  int len = strlen(tmp), j = 0;
  for (int i = 0; i < len; i++) {
    out[j++] = tmp[i];
    int rem = len - 1 - i;
    if (rem > 0 && rem % 3 == 0) out[j++] = '.';
  }
  out[j] = 0;
}

void beep(unsigned int ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}

void beepDone() {
  beep(150); delay(100); beep(150); delay(100); beep(400);
}

void updateButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 7; i++) {
    bool raw = (digitalRead(PIN_BTN[i]) == LOW);
    pressed[i] = false;
    if (raw != rawLast[i]) { rawLast[i] = raw; changeAt[i] = now; }
    if ((now - changeAt[i]) > DEBOUNCE_MS && raw != stableNow[i]) {
      stableNow[i] = raw;
      if (raw) pressed[i] = true;
    }
  }
}

void showMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Pilih Nominal:"));
  lcd.setCursor(0, 1); lcd.print(F("5 10 20 50 100 k"));
  state = ST_MENU;
  stateSince = millis();
}

void powerOn() {
  lcd.backlight();
  lcd.display();
  lcd.clear();
  char buf[16]; formatRp(total, buf);
  lcd.setCursor(0, 0); lcd.print(F("Total Tabungan:"));
  lcd.setCursor(0, 1); lcd.print(F("Rp ")); lcd.print(buf);
  beep(100);
  state = ST_INTRO;
  stateSince = millis();
  lastActivity = millis();
}

void powerOff() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("  Sampai jumpa  "));
  beep(300);
  delay(600);
  lcd.clear();
  lcd.noBacklight();
  state = ST_OFF;
  resetStart = 0; resetDone = false; lastSec = -1;
}

void addMoney(unsigned long amt) {
  total += amt;
  EEPROM.put(EEPROM_ADDR, total);

  char a[16], t[16];
  formatRp(amt, a);
  formatRp(total, t);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print('+'); lcd.print(a); lcd.print(F(" Total:"));
  lcd.setCursor(0, 1); lcd.print(F("Rp ")); lcd.print(t);

  beepDone();
  state = ST_RESULT;
  stateSince = millis();
}
bool handleReset(unsigned long now) {
  if (!stableNow[6]) {
    bool was = (resetStart != 0) || resetDone;
    resetStart = 0; resetDone = false; lastSec = -1;
    if (was) showMenu();
    return false;
  }
  if (resetDone) return true;
  if (resetStart == 0) resetStart = now;

  unsigned long held = now - resetStart;
  if (held >= RESET_HOLD_MS) {
    total = 0;
    EEPROM.put(EEPROM_ADDR, total);
    resetDone = true;
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("Total di-reset"));
    lcd.setCursor(0, 1); lcd.print(F("Rp 0"));
    beepDone();
    return true;
  }

  int sec = 3 - (int)(held / 1000);
  if (sec != lastSec) {
    lastSec = sec;
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("Tahan utk Reset"));
    lcd.setCursor(0, 1); lcd.print(F("Sisa ")); lcd.print(sec); lcd.print(F(" detik"));
  }
  return true;
}

void setup() {
  for (int i = 0; i < 7; i++) {
    pinMode(PIN_BTN[i], INPUT_PULLUP);
    rawLast[i] = stableNow[i] = false;
  }
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  EEPROM.get(EEPROM_ADDR, total);
  if (total == 0xFFFFFFFFUL) {
    total = 0;
    EEPROM.put(EEPROM_ADDR, total);
  }

  lcd.begin();
  lcd.noBacklight();
}

void loop() {
  updateButtons();
  unsigned long now = millis();

  for (int i = 0; i < 7; i++) if (pressed[i]) lastActivity = now;

  if (state == ST_OFF) {
    if (pressed[0]) powerOn();
    return;
  }

  if (pressed[0]) { powerOff(); return; }

  if (handleReset(now)) return;

  for (int i = 0; i < 5; i++) {
    if (pressed[i + 1]) { addMoney(NOMINAL[i]); break; }
  }

  if (state == ST_INTRO  && (now - stateSince) >= INTRO_MS)  showMenu();
  if (state == ST_RESULT && (now - stateSince) >= RESULT_MS) showMenu();

  if ((now - lastActivity) >= AUTO_OFF_MS) powerOff();
}