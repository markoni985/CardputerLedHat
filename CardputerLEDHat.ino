#include <M5Cardputer.h>
#include <NimBLEDevice.h>
#include "mbedtls/aes.h"
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include "time.h"

// ================= CONFIG & GLOBALS =================
String targetMac = ""; // fallback if no SD config
String timeZone  = "CST6CDT,M3.2.0,M11.1.0";
String wifiSSID  = "", wifiPASS = "";

String inputText = "";
unsigned long lastKeyTime = 0;
const int debounceDelay = 150;

enum ScreenMode { MODE_BLE, MODE_PREDATOR, MODE_CLOCK };
ScreenMode currentMode = MODE_BLE;

#define CHAR_CMD    "d44bc439-abfd-45a2-b575-925416129600"
#define CHAR_UPLOAD "d44bc439-abfd-45a2-b575-92541612960a"
#define CHAR_NOTIFY "d44bc439-abfd-45a2-b575-925416129601"
#define CHAR_FD02   "0000fd02-0000-1000-8000-00805f9b34fb"

// ================= FONT =================
const uint8_t font5x8[][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
  {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
  {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
  {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
  {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
  {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
  {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
  {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
  {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
  {0x61,0x51,0x49,0x45,0x43}
};

// ================= MODE & COLOR STATE =================
int selectedModeIndex  = 1;
int selectedColorIndex = 0;
int previewOffset      = 0;

const char* modeNames[]  = {"FIXED","SCR-R","SCR-L"};
uint8_t     modeValues[] = {1, 3, 4};

const char* colorNames[]   = {"RED","GREEN","BLUE","YEL","WHT"};
uint8_t     colorsRGB[][3] = {
  {255,0,0},{0,255,0},{0,0,255},{255,255,0},{255,255,255}
};

// ================= BLE =================
NimBLEClient*              bleClient   = nullptr;
NimBLERemoteCharacteristic *cmdChar    = nullptr;
NimBLERemoteCharacteristic *uploadChar = nullptr;
NimBLERemoteCharacteristic *notifyChar = nullptr;
NimBLERemoteCharacteristic *fd02Char   = nullptr;

struct Resp { uint8_t data[64]; size_t len; };
QueueHandle_t respQueue;

// ================= AES =================
void aes_op(uint8_t *in, uint8_t *out, int mode) {
  uint8_t KEY[16] = {
    0x32,0x67,0x2f,0x79,0x74,0xad,0x43,0x45,
    0x1d,0x9c,0x6c,0x89,0x4a,0x0e,0x87,0x64
  };
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mode == MBEDTLS_AES_ENCRYPT) mbedtls_aes_setkey_enc(&aes, KEY, 128);
  else                              mbedtls_aes_setkey_dec(&aes, KEY, 128);
  mbedtls_aes_crypt_ecb(&aes, mode, in, out);
  mbedtls_aes_free(&aes);
}

// ================= BLE CALLBACK =================
void notifyCB(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  uint8_t dec[64] = {0};
  for (size_t i = 0; i + 16 <= len; i += 16)
    aes_op(data + i, dec + i, MBEDTLS_AES_DECRYPT);
  Resp r;
  r.len = min(len, (size_t)64);
  memcpy(r.data, dec, r.len);
  xQueueSend(respQueue, &r, 0);
}

bool waitFor(const char* token, uint32_t timeout = 5000) {
  Resp r;
  uint32_t start = millis();
  while (millis() - start < timeout) {
    if (xQueueReceive(respQueue, &r, 10) == pdTRUE)
      if (memmem(r.data, r.len, token, strlen(token))) return true;
  }
  return false;
}

// ================= BLE RESET =================
void resetBLE() {
  cmdChar = uploadChar = notifyChar = fd02Char = nullptr;
  if (bleClient) {
    if (bleClient->isConnected()) bleClient->disconnect();
    NimBLEDevice::deleteClient(bleClient);
    bleClient = nullptr;
  }
  Resp r;
  while (xQueueReceive(respQueue, &r, 0) == pdTRUE) {}
}

// ================= BLE CONNECT =================
// Restored to known-working approach exactly as it was
bool connectBLE() {
  bleClient = NimBLEDevice::createClient();
  NimBLEAddress addr(targetMac.c_str(), BLE_ADDR_PUBLIC);
  if (!bleClient->connect(addr)) return false;

  auto services = bleClient->getServices(true);
  for (auto svc : services) {
    if (!cmdChar)    cmdChar    = svc->getCharacteristic(CHAR_CMD);
    if (!uploadChar) uploadChar = svc->getCharacteristic(CHAR_UPLOAD);
    if (!notifyChar) notifyChar = svc->getCharacteristic(CHAR_NOTIFY);
    if (!fd02Char)   fd02Char   = svc->getCharacteristic(CHAR_FD02);
  }

  if (!cmdChar || !uploadChar) return false;

  if (notifyChar) notifyChar->subscribe(true, notifyCB);
  if (fd02Char)   fd02Char->subscribe(true, notifyCB);

  delay(500);
  return true;
}

// ================= CONVERSION HELPERS =================
uint8_t reverseBits(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

int textToColumns(const char* text, uint8_t* out, int max) {
  int idx = 0;
  for (int i = 0; i < (int)strlen(text); i++) {
    int fi = toupper(text[i]) - 32;
    if (fi < 0 || fi > 58) fi = 0;
    for (int c = 0; c < 5; c++) {
      uint16_t col16 = (uint16_t)reverseBits(font5x8[fi][c]) << 4;
      if (idx + 2 >= max) return idx;
      out[idx++] = col16 >> 8;
      out[idx++] = col16 & 0xFF;
    }
    if (i < (int)strlen(text) - 1) {
      if (idx + 2 >= max) return idx;
      out[idx++] = 0;
      out[idx++] = 0;
    }
  }
  return idx;
}

// ================= SEND TEXT =================
bool sendTextNow() {
  static uint8_t text_bytes[2048];
  int text_len = textToColumns(inputText.c_str(), text_bytes, sizeof(text_bytes));
  if (text_len <= 0) return false;

  int cols      = text_len / 2;
  int total_len = text_len + cols * 3;

  uint8_t* payload = (uint8_t*)malloc(total_len);
  if (!payload) return false;
  memcpy(payload, text_bytes, text_len);

  for (int i = 0; i < cols; i++) {
    payload[text_len + i * 3 + 0] = colorsRGB[selectedColorIndex][0];
    payload[text_len + i * 3 + 1] = colorsRGB[selectedColorIndex][1];
    payload[text_len + i * 3 + 2] = colorsRGB[selectedColorIndex][2];
  }

  // Send DATS header
  uint8_t b[16] = {0};
  b[0] = 9; b[1] = 'D'; b[2] = 'A'; b[3] = 'T'; b[4] = 'S';
  b[5] = total_len >> 8; b[6] = total_len & 0xFF;
  b[7] = text_len  >> 8; b[8] = text_len  & 0xFF;
  uint8_t enc[16];
  aes_op(b, enc, MBEDTLS_AES_ENCRYPT);
  cmdChar->writeValue(enc, 16, false);
  if (!waitFor("DATSOK")) { free(payload); return false; }

  // Upload chunks
  for (int off = 0, idx = 0; off < total_len; off += 98, idx++) {
    int sz = min(98, total_len - off);
    uint8_t chunk[100];
    chunk[0] = sz + 1;
    chunk[1] = idx & 0xFF;
    memcpy(&chunk[2], &payload[off], sz);
    uploadChar->writeValue(chunk, sz + 2, false);
    if (!waitFor("REOK")) { free(payload); return false; }
  }

  // Commit
  uint8_t cp[16] = {5, 'D', 'A', 'T', 'C', 'P'};
  aes_op(cp, enc, MBEDTLS_AES_ENCRYPT);
  cmdChar->writeValue(enc, 16, false);
  waitFor("DATCPOK");

  // Set scroll mode
  uint8_t m[16] = {5, 'M', 'O', 'D', 'E', modeValues[selectedModeIndex]};
  aes_op(m, enc, MBEDTLS_AES_ENCRYPT);
  cmdChar->writeValue(enc, 16, false);
  waitFor("MODEOK", 2000);

  free(payload);
  return true;
}

// ================= UI HELPERS =================
uint16_t getColor565() {
  return M5.Lcd.color565(
    colorsRGB[selectedColorIndex][0],
    colorsRGB[selectedColorIndex][1],
    colorsRGB[selectedColorIndex][2]
  );
}

void drawPreview() {
  M5.Lcd.fillRect(0, 60, 240, 40, BLACK);
  if (inputText.length() == 0) return;
  String text = inputText + "   ";
  int len = text.length();
  for (int i = 0; i < 20; i++) {
    char c = text[(i + previewOffset) % len];
    M5.Lcd.setCursor(5 + i * 12, 70);
    M5.Lcd.setTextColor(getColor565());
    M5.Lcd.setTextSize(2);
    M5.Lcd.print(c);
  }
  previewOffset = (previewOffset + 1) % len;
}

void drawBLEUI(String status = "READY") {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);

  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(5, 5);
  M5.Lcd.print("TXT:");
  M5.Lcd.setCursor(60, 5);
  M5.Lcd.print(inputText);

  M5.Lcd.setCursor(5, 25);
  M5.Lcd.setTextColor(getColor565());
  M5.Lcd.print(colorNames[selectedColorIndex]);
  M5.Lcd.setCursor(120, 25);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.print(modeNames[selectedModeIndex]);

  M5.Lcd.setCursor(5, 110);
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.print(status);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(5, 130);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.print("TAB:mode /: col ;: scr `:clr ENTER:send");

  drawPreview();
}

// ================= CLOCK SCREEN =================
void draw7Seg(int x, int y, int val, uint16_t color, int scale = 4) {
  const uint8_t segments[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
  uint8_t s = segments[val % 10];
  int w = 10 * scale, h = 18 * scale, t = 2 * scale;
  if (s & 0x01) M5.Lcd.fillRect(x + t,     y,           w - 2*t, t,       color);
  if (s & 0x02) M5.Lcd.fillRect(x + w - t, y + t,       t,       h/2 - t, color);
  if (s & 0x04) M5.Lcd.fillRect(x + w - t, y + h/2 + t, t,       h/2 - t, color);
  if (s & 0x08) M5.Lcd.fillRect(x + t,     y + h - t,   w - 2*t, t,       color);
  if (s & 0x10) M5.Lcd.fillRect(x,         y + h/2 + t, t,       h/2 - t, color);
  if (s & 0x20) M5.Lcd.fillRect(x,         y + t,       t,       h/2 - t, color);
  if (s & 0x40) M5.Lcd.fillRect(x + t,     y + h/2 - t, w - 2*t, t,       color);
}

void drawClockScreen() {
  struct tm ti;
  bool useNTP = getLocalTime(&ti);
  M5.Lcd.fillScreen(BLACK);
  int hh = useNTP ? ti.tm_hour : (millis() / 3600000) % 24;
  int mm = useNTP ? ti.tm_min  : (millis() / 60000)   % 60;
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(useNTP ? GREEN : RED);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print(useNTP ? "NTP" : "UPTIME");
  draw7Seg(35,  40, hh / 10, CYAN);
  draw7Seg(80,  40, hh % 10, CYAN);
  if ((millis() / 500) % 2) M5.Lcd.fillCircle(128, 60, 4, WHITE);
  draw7Seg(145, 40, mm / 10, CYAN);
  draw7Seg(190, 40, mm % 10, CYAN);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.setCursor(5, 130);
  M5.Lcd.print("TAB: switch mode");
}

// ================= PREDATOR SCREEN =================
void drawPredatorMode() {
  static unsigned long lastUpdate = 0;
  static uint8_t glyphStates[4];

  if (millis() - lastUpdate > 400) {
    lastUpdate = millis();
    for (int i = 0; i < 4; i++) glyphStates[i] = random(0, 255);
  }

  M5.Lcd.fillScreen(BLACK);
  const uint16_t glowRed = M5.Lcd.color565(80, 0, 0);

  for (int g = 0; g < 4; g++) {
    int cx = 35 + (g * 55);
    int cy = 60;
    uint8_t s = glyphStates[g];

    auto drawSeg = [&](int x1, int y1, int x2, int y2, bool active) {
      if (active) {
        M5.Lcd.drawLine(cx+x1-1, cy+y1, cx+x2-1, cy+y2, glowRed);
        M5.Lcd.drawLine(cx+x1,   cy+y1, cx+x2,   cy+y2, RED);
      } else {
        M5.Lcd.drawLine(cx+x1, cy+y1, cx+x2, cy+y2, M5.Lcd.color565(30, 0, 0));
      }
    };

    drawSeg(-10, -40, -10, -25, bitRead(s, 0));
    drawSeg( 10, -40,  10, -25, bitRead(s, 1));
    drawSeg(-10, -25, -20, -10, bitRead(s, 2));
    drawSeg( 10, -25,  20, -10, bitRead(s, 3));
    drawSeg(-20,  10, -10,  25, bitRead(s, 4));
    drawSeg( 20,  10,  10,  25, bitRead(s, 5));
    drawSeg(-10,  25, -10,  40, bitRead(s, 6));
    drawSeg( 10,  25,  10,  40, bitRead(s, 7));

    if (random(0, 5) > 2) M5.Lcd.drawFastHLine(cx - 15, cy, 30, RED);
  }

  M5.Lcd.setCursor(45, 115);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.setTextSize(1);
  M5.Lcd.print("FINAL COUNTDOWN");
}

// ================= SETUP =================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5Cardputer.begin(cfg, true);

  if (SD.begin(GPIO_NUM_12, SPI, 15000000)) {
    File file = SD.open("/config.txt");
    if (file) {
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.startsWith("MAC="))  targetMac = line.substring(4);
        if (line.startsWith("TZ="))   timeZone  = line.substring(3);
        if (line.startsWith("SSID=")) wifiSSID  = line.substring(5);
        if (line.startsWith("PASS=")) wifiPASS  = line.substring(5);
      }
      file.close();
    }
  }

  if (wifiSSID != "") {
    WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 6000) delay(100);
    if (WiFi.status() == WL_CONNECTED)
      configTzTime(timeZone.c_str(), "pool.ntp.org");
  }

  respQueue = xQueueCreate(10, sizeof(Resp));
  NimBLEDevice::init("");
  drawBLEUI();
}

// ================= LOOP =================
void loop() {
  M5Cardputer.update();

  // TAB cycles through the three screen modes
  if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
    currentMode = (ScreenMode)((currentMode + 1) % 3);
    M5.Lcd.fillScreen(BLACK);
    if (currentMode == MODE_BLE) drawBLEUI();
    delay(250);
    return;
  }

  // ---- CLOCK MODE ----
  if (currentMode == MODE_CLOCK) {
    static unsigned long lastClock = 0;
    if (millis() - lastClock > 1000) { drawClockScreen(); lastClock = millis(); }
    return;
  }

  // ---- PREDATOR MODE ----
  if (currentMode == MODE_PREDATOR) {
    static unsigned long lastPred = 0;
    if (millis() - lastPred > 150) { drawPredatorMode(); lastPred = millis(); }
    return;
  }

  // ---- BLE / TEXT MODE ----

  // Rolling preview scroll
  static unsigned long lastScroll = 0;
  if (millis() - lastScroll > 150) { drawPreview(); lastScroll = millis(); }

  // Key handling with debounce
  if (M5Cardputer.Keyboard.isPressed() && millis() - lastKeyTime > debounceDelay) {
    lastKeyTime = millis();
    bool updateScreen = false;

    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
      drawBLEUI("CONNECTING...");
      resetBLE();
      if (connectBLE()) {
        drawBLEUI("SENDING...");
        if (sendTextNow()) drawBLEUI("SUCCESS");
        else               drawBLEUI("SEND FAIL");
        bleClient->disconnect();
      } else {
        drawBLEUI("BLE ERROR");
      }
      resetBLE();
      delay(1200);
      drawBLEUI();
    }
    else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
      if (inputText.length() > 0) { inputText.remove(inputText.length() - 1); updateScreen = true; }
    }
    else if (M5Cardputer.Keyboard.isKeyPressed('`')) {
      inputText = "";
      updateScreen = true;
    }
    else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
      selectedColorIndex = (selectedColorIndex + 1) % 5;
      updateScreen = true;
    }
    else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
      selectedColorIndex = (selectedColorIndex + 4) % 5;
      updateScreen = true;
    }
    else if (M5Cardputer.Keyboard.isKeyPressed(';')) {
      selectedModeIndex = (selectedModeIndex + 1) % 3;
      updateScreen = true;
    }
    else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
      selectedModeIndex = (selectedModeIndex + 2) % 3;
      updateScreen = true;
    }
    else {
      for (auto c : M5Cardputer.Keyboard.keysState().word) {
        if (inputText.length() < 40) {
          inputText += (char)toupper(c);
          updateScreen = true;
        }
      }
    }

    if (updateScreen) drawBLEUI();
  }
}
