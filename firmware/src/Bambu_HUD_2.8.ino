#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <Update.h>
#include <time.h>

// =====================
// Hardware Profile
// =====================
// Select ONE screen profile before compiling.
//
// For your original 4 inch 480x320 HUD, leave this as HUD_SCREEN_4IN.
// For the ESP32-2432S028 / CYD 2.8 inch 320x240 display, comment HUD_SCREEN_4IN
// and uncomment HUD_SCREEN_CYD28. You must also switch TFT_eSPI's
// User_Setup_Select.h to the matching display setup file.
// #define HUD_SCREEN_4IN
#define HUD_SCREEN_CYD28

#define BOOT_BUTTON_PIN 0  // Hold BOOT at startup for factory reset

#if defined(HUD_SCREEN_CYD28)
  // ESP32-2432S028 / Cheap Yellow Display / CYD
  #define SCREEN_W 320
  #define SCREEN_H 240
  #define TFT_BACKLIGHT_PIN 21
  #define TOUCH_CS 33
  #define TOUCH_SPI_SCLK 25
  #define TOUCH_SPI_MISO 39
  #define TOUCH_SPI_MOSI 32
  #define TOUCH_MAP_X_MIN 450
  #define TOUCH_MAP_X_MAX 3650
  #define TOUCH_MAP_Y_MIN 620
  #define TOUCH_MAP_Y_MAX 3500
  #define HAS_BATTERY_MONITOR 0
  // CYD has no battery sense pin. Keep a dummy define so shared code compiles.
  #define BAT_ADC_PIN -1
#else
  // Original 4 inch 480x320 HUD display
  #define SCREEN_W 480
  #define SCREEN_H 320
  #define TFT_BACKLIGHT_PIN 27
  #define BAT_ADC_PIN 34     // Confirmed battery voltage sense pin
  #define TOUCH_CS 33
  #define TOUCH_SPI_SCLK 14
  #define TOUCH_SPI_MISO 12
  #define TOUCH_SPI_MOSI 13
  #define TOUCH_MAP_X_MIN 3900
  #define TOUCH_MAP_X_MAX 700
  #define TOUCH_MAP_Y_MIN 3900
  #define TOUCH_MAP_Y_MAX 350
  #define HAS_BATTERY_MONITOR 1
#endif

#define BOTTOM_NAV_Y (SCREEN_H - 35)

XPT2046_Touchscreen ts(TOUCH_CS);

// =====================
// Objects
// =====================
TFT_eSPI tft;
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

const byte DNS_PORT = 53;

// =====================
// Saved Settings
// =====================
String wifiSSID;
String wifiPASS;

// Dynamic printer list
#define MAX_PRINTERS 10

struct PrinterConfig {
  String name;
  String ip;
  String accessCode;
  String serial;
};

PrinterConfig printers[MAX_PRINTERS];
int printerCount = 0;
int selectedPrinter = -1;

// Compatibility variables used by existing MQTT/HUD code.
// These are synced from printers[selectedPrinter].
String printerIP;
String accessCode;
String printerSerial;
String printerName;

// =====================
// Printer Data
// =====================
float nozzleTemp = 0;
float bedTemp = 0;
float chamberTemp = 0;

int partFanSpeed = -1;
int auxFanSpeed = -1;
int chamberFanSpeed = -1;

String amsSummary = "";
String amsType[4];
String amsColor[4];
bool amsLoaded[4] = {false, false, false, false};
String lastAmsSummary = "";
String manualAmsColor[4] = {"#222222", "#222222", "#222222", "#222222"};
int selectedAmsColorSlot = 0;
int screenRotation = 1;  // 1 = normal landscape, 3 = 180 degree rotated landscape
int themeID = 0;          // 0 Green, 1 Blue, 2 Carbon, 3 Matrix, 4 OLED, 5 Red, 6 Orange
// Built-in TFT_eSPI font style: 0 = Classic, 1 = Modern, 2 = Bold, 3 = Digital, 4 = Compact
// Uses only built-in TFT_eSPI fonts so no extra font files are required.
int displayStyle = 0;

// =====================
// Power Save / Backlight
// =====================
// 0 = Disabled, 1 = Dim Screen, 2 = Screen Off
int powerSaveMode = 0;
// 0 = Never, 1 = 1 min, 2 = 5 min, 3 = 10 min, 4 = 15 min, 5 = 30 min, 6 = 60 min
int powerSaveTimeoutIndex = 2;
unsigned long lastUserActivity = 0;
bool powerSaveActive = false;

int progress = 0;
int layerNum = 0;
int totalLayers = 0;
int remainingMinutes = 0;
String etaText = "--";
String lastEtaText = "";

String printerState = "Connected";
String fileName = "";
String displayedFileName = "";
String wifiSignal = "";

// =====================
// Print Completion Handling
// =====================
unsigned long printCompletedAt = 0;
bool printCompletionPendingReset = false;
const unsigned long PRINT_COMPLETE_RESET_DELAY = 120000UL; // 2 minutes

// =====================
// Battery Data
// =====================
// GPIO34 was confirmed on your board as the battery voltage sense pin.
// Calibrated against multimeter reading: 4.163V actual battery voltage.
float batteryVoltage = 0.0;
float smoothedBatteryVoltage = 0.0;
int batteryPercent = -1;
bool batteryChargingEstimate = false;
bool lowBatteryWarning = false;
unsigned long lastBatteryRead = 0;
int lastBatteryPercent = -999;
float lastBatteryVoltage = -999;
bool lastBatteryChargingEstimate = false;

// =====================
// Clock Data
// =====================
// Time is set automatically from NTP after WiFi connects.
// Default is Pacific Daylight Time: UTC-7 hours.
int clockUtcOffsetMinutes = -420;
bool clockUse24Hour = false;
String hudClockText = "--:--";
String lastHudClockText = "";
unsigned long lastClockRead = 0;
unsigned long lastHeartbeat = 0;

// =====================
// Display Cache
// =====================
bool hasConfig = false;
bool mqttConnected = false;
bool hudDrawn = false;

unsigned long lastScreenUpdate = 0;

float lastNozzleTemp = -999;
float lastBedTemp = -999;
float lastChamberTemp = -999;

int lastPartFanSpeed = -999;
int lastAuxFanSpeed = -999;
int lastChamberFanSpeed = -999;

int lastProgress = -1;
int lastRemainingMinutes = -1;
int lastLayerNum = -1;

String lastPrinterState = "";
String lastWifiSignal = "";
String lastDisplayedFileName = "";

// =====================
// Pages
// =====================
#define PAGE_DASHBOARD 0
#define PAGE_PRINTERS  1
#define PAGE_NETWORK   PAGE_PRINTERS
#define PAGE_SETTINGS  2
#define PAGE_AMS_COLORS 3
#define PAGE_STATS 4

int currentPage = PAGE_DASHBOARD;

// =====================
// Forward Declarations
// =====================
bool connectWiFi();
bool connectMQTT();
void requestFullStatus();
void drawHUD();
void drawNetworkPage();
void drawSettingsPage();
void drawAMSColorsPage();
void drawStatsPage();
void handleTouch();
void setupOTA();
void syncSelectedPrinter();
void saveSettings();
void handlePrintersPage();
void handleSaveWifi();
void handleSaveTime();
void handleAddPrinter();
void handleDeletePrinter();
void handleSelectPrinter();
void handleReboot();
void handleRequestStatus();
void resetPrinterRuntimeData();
int readFanPercent(JsonVariant value);
void parseAMS(JsonObject print);
void setActiveAMSSlot(int rawSlot);
uint16_t amsColorToTFT(String color);
String getAMSDisplayColor(int slot);
void applyScreenRotation();
String getThemeName();
String getDisplayStyleName();
void useClassicFont(uint8_t size = 2);
void useHeaderFont();
void useLabelFont();
void useValueFont();
void useClockFont();
void checkFactoryReset();
void updateBatteryStatus();
void drawBatteryIndicator();
int batteryVoltageToPercent(float voltage);
void configureClock();
void updateClockText();
void drawClockIndicator();
String getClockOffsetLabel();
String getETAText();
String getUptimeText();
String normalizePrinterState(String rawState);
void markPrintComplete();
void clearCompletedPrintData();
void updateCompletedPrintReset();
String getPowerSaveModeLabel();
String getPowerSaveTimeoutLabel();
unsigned long getPowerSaveTimeoutMs();
void setBacklightLevel(int level);
void wakeDisplay();
void updatePowerSave();


void applyScreenRotation() {
  tft.setRotation(screenRotation);
  ts.setRotation(screenRotation);
}


void useClassicFont(uint8_t size) {
  // Built-in TFT_eSPI fonts only. This avoids external FreeFont compile errors.
  tft.setTextFont(1);
  tft.setTextSize(size);
}

void useHeaderFont() {
  // Header text should stay compact.
  switch (displayStyle) {
    case 1:  // Modern
      tft.setTextFont(2);
      tft.setTextSize(1);
      break;
    case 2:  // Bold
      tft.setTextFont(4);
      tft.setTextSize(1);
      break;
    case 3:  // Digital
      tft.setTextFont(4);
      tft.setTextSize(1);
      break;
    case 4:  // Compact
      tft.setTextFont(1);
      tft.setTextSize(2);
      break;
    default:
      useClassicFont(2);
      break;
  }
}

void useLabelFont() {
  switch (displayStyle) {
    case 1:  // Modern
      tft.setTextFont(2);
      tft.setTextSize(1);
      break;
    case 2:  // Bold
      tft.setTextFont(2);
      tft.setTextSize(1);
      break;
    case 3:  // Digital
      tft.setTextFont(2);
      tft.setTextSize(1);
      break;
    case 4:  // Compact
      tft.setTextFont(1);
      tft.setTextSize(2);
      break;
    default:
      useClassicFont(2);
      break;
  }
}

void useValueFont() {
  switch (displayStyle) {
    case 1:  // Modern
      tft.setTextFont(4);
      tft.setTextSize(1);
      break;
    case 2:  // Bold
      tft.setTextFont(4);
      tft.setTextSize(1);
      break;
    case 3:  // Digital
      tft.setTextFont(4);
      tft.setTextSize(1);
      break;
    case 4:  // Compact
      tft.setTextFont(2);
      tft.setTextSize(1);
      break;
    default:
      useClassicFont(2);
      break;
  }
}

void useClockFont() {
  switch (displayStyle) {
    case 3:  // Digital clock look
      tft.setTextFont(7);
      tft.setTextSize(1);
      break;
    case 1:
    case 2:
      tft.setTextFont(4);
      tft.setTextSize(1);
      break;
    case 4:
      tft.setTextFont(2);
      tft.setTextSize(1);
      break;
    default:
      useClassicFont(2);
      break;
  }
}

String getPowerSaveModeLabel() {
  if (powerSaveMode == 1) return "Dim";
  if (powerSaveMode == 2) return "Off";
  return "Disabled";
}

String getPowerSaveTimeoutLabel() {
  switch (powerSaveTimeoutIndex) {
    case 1: return "1 min";
    case 2: return "5 min";
    case 3: return "10 min";
    case 4: return "15 min";
    case 5: return "30 min";
    case 6: return "60 min";
    default: return "Never";
  }
}

unsigned long getPowerSaveTimeoutMs() {
  switch (powerSaveTimeoutIndex) {
    case 1: return 1UL * 60UL * 1000UL;
    case 2: return 5UL * 60UL * 1000UL;
    case 3: return 10UL * 60UL * 1000UL;
    case 4: return 15UL * 60UL * 1000UL;
    case 5: return 30UL * 60UL * 1000UL;
    case 6: return 60UL * 60UL * 1000UL;
    default: return 0;
  }
}

void setBacklightLevel(int level) {
  level = constrain(level, 0, 255);
  analogWrite(TFT_BACKLIGHT_PIN, level);
}

void wakeDisplay() {
  lastUserActivity = millis();

  if (powerSaveActive) {
    powerSaveActive = false;
    setBacklightLevel(255);

    // Force a clean redraw after waking from dim/off.
    hudDrawn = false;
    lastPrinterState = "";
    lastWifiSignal = "";
    lastProgress = -1;
    lastNozzleTemp = -999;
    lastBedTemp = -999;
    lastLayerNum = -1;
    lastRemainingMinutes = -1;
    lastDisplayedFileName = "__FORCE__";
    lastAmsSummary = "__FORCE__";
    lastEtaText = "__FORCE__";
  }
}

void updatePowerSave() {
  if (powerSaveMode == 0 || powerSaveTimeoutIndex == 0) {
    if (powerSaveActive) wakeDisplay();
    return;
  }

  unsigned long timeoutMs = getPowerSaveTimeoutMs();
  if (timeoutMs == 0) return;

  if (!powerSaveActive && millis() - lastUserActivity >= timeoutMs) {
    powerSaveActive = true;

    if (powerSaveMode == 1) {
      // Dim screen but keep the current HUD visible.
      setBacklightLevel(40);
    }
    else if (powerSaveMode == 2) {
      // Screen off. Touch wakes it.
      setBacklightLevel(0);
    }
  }
}


void checkFactoryReset() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Do NOT hold BOOT while powering on. On many ESP32 boards that enters
  // download/flash mode before this sketch can run.
  //
  // Correct use:
  // 1) Power on normally.
  // 2) When this screen appears, hold BOOT for 5 seconds.
  // 3) Settings are cleared and the HUD restarts into setup mode.

  unsigned long resetWindowStart = millis();
  bool promptDrawn = false;

  while (millis() - resetWindowStart < 10000) {
    if (!promptDrawn) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextSize(2);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("Bambu HUD", 40, 45);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("Hold BOOT now", 40, 100);
      tft.drawString("5 sec = reset", 40, 130);

      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("Starting shortly...", 40, 185);

      promptDrawn = true;
    }

    // BOOT is active-low.
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextSize(2);

      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("Factory Reset", 40, 80);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("Keep holding BOOT", 40, 125);

      unsigned long holdStart = millis();

      while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        unsigned long held = millis() - holdStart;
        int secondsLeft = 5 - (held / 1000);
        if (secondsLeft < 0) secondsLeft = 0;

        tft.fillRect(40, 170, 320, 30, TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("Reset in " + String(secondsLeft), 40, 170);

        if (held >= 5000) {
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_RED, TFT_BLACK);
          tft.drawString("Clearing settings...", 40, 120);

          prefs.begin("bambu", false);
          prefs.clear();
          prefs.end();

          delay(1200);
          ESP.restart();
        }

        delay(100);
      }

      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("Reset cancelled", 40, 130);
      delay(900);
      tft.fillScreen(TFT_BLACK);
      return;
    }

    delay(50);
  }

  tft.fillScreen(TFT_BLACK);
}



// =====================
// Battery Monitor
// =====================
int batteryVoltageToPercent(float voltage) {
  // Li-ion voltage estimate using a table instead of a straight linear map.
  // This makes 4.10V show around 90% instead of about 60%.
  if (voltage >= 4.20) return 100;
  if (voltage >= 4.15) return 95;
  if (voltage >= 4.10) return 90;
  if (voltage >= 4.05) return 85;
  if (voltage >= 4.00) return 80;
  if (voltage >= 3.95) return 72;
  if (voltage >= 3.90) return 65;
  if (voltage >= 3.85) return 58;
  if (voltage >= 3.80) return 50;
  if (voltage >= 3.75) return 42;
  if (voltage >= 3.70) return 35;
  if (voltage >= 3.65) return 28;
  if (voltage >= 3.60) return 20;
  if (voltage >= 3.50) return 10;
  if (voltage >= 3.30) return 5;
  return 0;
}

void updateBatteryStatus() {
#if HAS_BATTERY_MONITOR == 0
  batteryVoltage = 0.0;
  batteryPercent = -1;
  batteryChargingEstimate = false;
  lowBatteryWarning = false;
  return;
#endif

  // Average several ADC samples for a stable display.
  // Battery calibration multiplier: 2.11 based on multimeter reading of 4.163V.
  const int samples = 16;
  long total = 0;

  for (int i = 0; i < samples; i++) {
    total += analogRead(BAT_ADC_PIN);
    delay(2);
  }

  int raw = total / samples;
  float adcVoltage = raw * 3.3 / 4095.0;
  float instantVoltage = adcVoltage * 2.11;

  // Smooth the displayed voltage so it does not jump around.
  if (smoothedBatteryVoltage <= 0.1) {
    smoothedBatteryVoltage = instantVoltage;
  }
  else {
    smoothedBatteryVoltage = (smoothedBatteryVoltage * 0.88) + (instantVoltage * 0.12);
  }

  batteryVoltage = smoothedBatteryVoltage;
  batteryPercent = batteryVoltageToPercent(batteryVoltage);

  // No dedicated USB/charge-status pin was found on this board.
  // Use the instant reading, not the smoothed reading, so the HUD changes faster
  // when you plug/unplug USB. This is still only an estimate.
  batteryChargingEstimate = (instantVoltage >= 3.92);

  // Only warn about low battery when we are likely running from battery.
  lowBatteryWarning = (batteryPercent >= 0 && batteryPercent <= 20);
}

void drawBatteryIndicator() {
#if HAS_BATTERY_MONITOR == 0
  return;
#endif

  if (batteryPercent < 0) return;

  // Clear the larger right side of the header. WiFi was removed from the HUD
  // header so the battery display has enough room to look clean.
  tft.fillRect(330, 8, 142, 25, themePanel());

  int x = 342;
  int y = 12;
  int w = 34;
  int h = 16;

  uint16_t levelColor = themeGreen();
  if (batteryPercent <= 20) {
    levelColor = TFT_RED;
  }
  else if (batteryPercent <= 45) {
    levelColor = TFT_YELLOW;
  }

  // Battery outline and terminal. This is shown in both USB and battery mode.
  tft.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  tft.fillRect(x + w, y + 5, 4, 6, TFT_WHITE);

  int fillW = map(batteryPercent, 0, 100, 0, w - 5);
  if (fillW > 0) {
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, levelColor);
  }

  // Show percentage only. We no longer color the icon based on the guessed
  // USB/charging state because voltage alone is not a reliable charge detector.
  useClassicFont(2);
  tft.setTextColor(lowBatteryWarning ? TFT_RED : TFT_WHITE, themePanel());
  tft.drawString(String(batteryPercent) + "%", 388, 12);
}


// =====================
// Clock
// =====================
void configureClock() {
  // NTP sets the clock. The offset is user-configurable from the web portal.
  configTime(clockUtcOffsetMinutes * 60, 0, "pool.ntp.org", "time.nist.gov");
}

String getClockOffsetLabel() {
  int total = clockUtcOffsetMinutes;
  String sign = total >= 0 ? "+" : "-";
  total = abs(total);
  int h = total / 60;
  int m = total % 60;

  String label = "UTC" + sign + String(h);
  if (m > 0) {
    label += ":";
    if (m < 10) label += "0";
    label += String(m);
  }
  return label;
}

void updateClockText() {
  time_t now = time(nullptr);

  // Before NTP sync, time_t will be very small/invalid.
  if (now < 1600000000) {
    hudClockText = "--:--";
    return;
  }

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);

  char buffer[12];
  if (clockUse24Hour) {
    strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
    hudClockText = String(buffer);
  }
  else {
    strftime(buffer, sizeof(buffer), "%I:%M%p", &timeInfo);
    hudClockText = String(buffer);
    if (hudClockText.startsWith("0")) hudClockText.remove(0, 1);
    hudClockText.replace("AM", "a");
    hudClockText.replace("PM", "p");
  }
}

String getETAText() {
  if (remainingMinutes <= 0) return "--";

  time_t now = time(nullptr);
  if (now < 1600000000) return "--";

  time_t finishTime = now + ((time_t)remainingMinutes * 60);
  struct tm nowInfo;
  struct tm finishInfo;

  localtime_r(&now, &nowInfo);
  localtime_r(&finishTime, &finishInfo);

  char buffer[16];
  if (clockUse24Hour) {
    strftime(buffer, sizeof(buffer), "%H:%M", &finishInfo);
  }
  else {
    strftime(buffer, sizeof(buffer), "%I:%M%p", &finishInfo);
  }

  String out = String(buffer);
  if (!clockUse24Hour) {
    if (out.startsWith("0")) out.remove(0, 1);
    out.replace("AM", "a");
    out.replace("PM", "p");
  }

  if (finishInfo.tm_yday != nowInfo.tm_yday || finishInfo.tm_year != nowInfo.tm_year) {
    out = "Tmr " + out;
  }

  return out;
}

String getUptimeText() {
  unsigned long totalSeconds = millis() / 1000;
  unsigned long days = totalSeconds / 86400;
  totalSeconds %= 86400;
  unsigned long hours = totalSeconds / 3600;
  totalSeconds %= 3600;
  unsigned long minutes = totalSeconds / 60;

  if (days > 0) {
    return String(days) + "d " + String(hours) + "h";
  }

  return String(hours) + "h " + String(minutes) + "m";
}

void drawClockIndicator() {
  int x = 170;
  int y = 14;

  tft.fillRect(x, 8, 112, 25, themePanel());
  useClockFont();
  tft.setTextColor(themeGreen(), themePanel());
  tft.drawString(hudClockText, x, y);
}


// =====================
// Setup Screens
// =====================
void drawSetupScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  tft.drawString("Bambu HUD Setup", 20, 20);
  tft.drawLine(0, 55, 480, 55, TFT_DARKGREY);

  tft.drawString("Connect to WiFi:", 20, 85);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("BambuHUD_Setup", 20, 115);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Password:", 20, 155);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("bambuhud", 20, 185);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Open browser:", 20, 225);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("192.168.4.1", 20, 255);
}

void drawSavedScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Settings Saved!", 40, 100);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Restarting...", 40, 140);
}

void drawNormalScreen(String msg) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  tft.drawString("Bambu HUD", 20, 20);
  tft.drawLine(0, 50, 480, 50, TFT_DARKGREY);
  tft.drawString(msg, 20, 90);
}

// =====================
// Settings
// =====================
void syncSelectedPrinter() {
  if (selectedPrinter >= 0 && selectedPrinter < printerCount) {
    printerName = printers[selectedPrinter].name;
    printerIP = printers[selectedPrinter].ip;
    accessCode = printers[selectedPrinter].accessCode;
    printerSerial = printers[selectedPrinter].serial;
  }
  else {
    selectedPrinter = -1;
    printerName = "";
    printerIP = "";
    accessCode = "";
    printerSerial = "";
  }
}

void resetPrinterRuntimeData() {
  nozzleTemp = 0;
  bedTemp = 0;
  chamberTemp = 0;
  partFanSpeed = -1;
  auxFanSpeed = -1;
  chamberFanSpeed = -1;
  amsSummary = "";
  lastAmsSummary = "";
  for (int i = 0; i < 4; i++) {
    amsType[i] = "";
    amsColor[i] = "";
    amsLoaded[i] = false;
  }
  progress = 0;
  layerNum = 0;
  totalLayers = 0;
  remainingMinutes = 0;
  printerState = "Disconnected";
  fileName = "";
  displayedFileName = "";
  wifiSignal = "";

  lastPrinterState = "";
  lastWifiSignal = "";
  lastProgress = -1;
  lastNozzleTemp = -999;
  lastBedTemp = -999;
  lastChamberTemp = -999;
  lastPartFanSpeed = -999;
  lastAuxFanSpeed = -999;
  lastChamberFanSpeed = -999;
  lastLayerNum = -1;
  lastRemainingMinutes = -1;
  lastDisplayedFileName = "__FORCE__";
}

void loadSettings() {
  prefs.begin("bambu", true);

  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  screenRotation = prefs.getInt("rotation", 1);
  if (screenRotation != 1 && screenRotation != 3) screenRotation = 1;

  themeID = prefs.getInt("theme", 0);
  if (themeID < 0 || themeID > 6) themeID = 0;

  displayStyle = prefs.getInt("display_style", 0);
  if (displayStyle < 0 || displayStyle > 4) displayStyle = 0;

  powerSaveMode = prefs.getInt("ps_mode", 0);
  if (powerSaveMode < 0 || powerSaveMode > 2) powerSaveMode = 0;

  powerSaveTimeoutIndex = prefs.getInt("ps_timeout", 2);
  if (powerSaveTimeoutIndex < 0 || powerSaveTimeoutIndex > 6) powerSaveTimeoutIndex = 2;

  clockUtcOffsetMinutes = prefs.getInt("clock_offset", -420);
  if (clockUtcOffsetMinutes < -720 || clockUtcOffsetMinutes > 840) clockUtcOffsetMinutes = -420;
  clockUse24Hour = prefs.getBool("clock_24h", false);

  printerCount = prefs.getInt("printer_count", 0);
  selectedPrinter = prefs.getInt("selected_printer", -1);

  if (printerCount < 0) printerCount = 0;
  if (printerCount > MAX_PRINTERS) printerCount = MAX_PRINTERS;

  for (int i = 0; i < MAX_PRINTERS; i++) {
    printers[i].name = "";
    printers[i].ip = "";
    printers[i].accessCode = "";
    printers[i].serial = "";
  }

  for (int i = 0; i < printerCount; i++) {
    String prefix = "p" + String(i) + "_";
    printers[i].name = prefs.getString((prefix + "name").c_str(), "Printer " + String(i + 1));
    printers[i].ip = prefs.getString((prefix + "ip").c_str(), "");
    printers[i].accessCode = prefs.getString((prefix + "code").c_str(), "");
    printers[i].serial = prefs.getString((prefix + "serial").c_str(), "");
  }

  // Import old single-printer settings one time if no dynamic printer list exists yet.
  if (printerCount == 0) {
    String oldIP = prefs.getString("ip", "");
    String oldCode = prefs.getString("code", "");
    String oldSerial = prefs.getString("serial", "");

    if (oldIP.length() > 0 && oldCode.length() > 0 && oldSerial.length() > 0) {
      printers[0].name = "Bambu Printer";
      printers[0].ip = oldIP;
      printers[0].accessCode = oldCode;
      printers[0].serial = oldSerial;
      printerCount = 1;
      selectedPrinter = 0;
    }
  }

  for (int i = 0; i < 4; i++) {
    manualAmsColor[i] = prefs.getString(("ams_c" + String(i)).c_str(), "#222222");
  }

  prefs.end();

  if (selectedPrinter < 0 && printerCount > 0) selectedPrinter = 0;
  if (selectedPrinter >= printerCount) selectedPrinter = 0;

  syncSelectedPrinter();

  hasConfig =
    wifiSSID.length() > 0 &&
    printerCount > 0 &&
    selectedPrinter >= 0 &&
    printerIP.length() > 0 &&
    accessCode.length() > 0 &&
    printerSerial.length() > 0;
}




void saveSettings() {
  prefs.begin("bambu", false);

  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPASS);
  prefs.putInt("printer_count", printerCount);
  prefs.putInt("selected_printer", selectedPrinter);
  prefs.putInt("rotation", screenRotation);
  prefs.putInt("theme", themeID);
  prefs.putInt("display_style", displayStyle);
  prefs.putInt("ps_mode", powerSaveMode);
  prefs.putInt("ps_timeout", powerSaveTimeoutIndex);
  prefs.putInt("clock_offset", clockUtcOffsetMinutes);
  prefs.putBool("clock_24h", clockUse24Hour);

  for (int i = 0; i < MAX_PRINTERS; i++) {
    String prefix = "p" + String(i) + "_";

    if (i < printerCount) {
      prefs.putString((prefix + "name").c_str(), printers[i].name);
      prefs.putString((prefix + "ip").c_str(), printers[i].ip);
      prefs.putString((prefix + "code").c_str(), printers[i].accessCode);
      prefs.putString((prefix + "serial").c_str(), printers[i].serial);
    }
    else {
      prefs.remove((prefix + "name").c_str());
      prefs.remove((prefix + "ip").c_str());
      prefs.remove((prefix + "code").c_str());
      prefs.remove((prefix + "serial").c_str());
    }
  }

  for (int i = 0; i < 4; i++) {
    prefs.putString(("ams_c" + String(i)).c_str(), manualAmsColor[i]);
  }

  prefs.end();

  syncSelectedPrinter();
}

// =====================
// Setup Portal
// =====================
String setupPage() {
  String html = "";

  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:white;padding:18px;}";
  html += "input{width:100%;padding:12px;margin:7px 0 14px 0;font-size:16px;box-sizing:border-box;background:#222;color:white;border:1px solid #444;}";
  html += "button,.btn{display:block;width:100%;padding:13px;margin:8px 0;background:#1fa463;color:white;border:0;font-size:17px;text-align:center;text-decoration:none;box-sizing:border-box;}";
  html += ".danger{background:#b63737;}.select{background:#276fe8;}.box{max-width:520px;margin:auto;}.card{background:#1b1b1b;padding:14px;margin:12px 0;border-radius:8px;border:1px solid #333;}";
  html += ".small{color:#aaa;font-size:13px;word-break:break-all;}";
  html += "</style></head>";

  html += "<body><div class='box'>";
  html += "<h2>Bambu HUD Setup</h2>";

  html += "<div class='card'>";
  html += "<h3>WiFi</h3>";
  html += "<form action='/saveWifi' method='POST'>";
  html += "WiFi SSID:<br>";
  html += "<input name='ssid' value='" + wifiSSID + "' required>";
  html += "WiFi Password:<br>";
  html += "<input name='pass' type='password' value='" + wifiPASS + "'>";
  html += "<button type='submit'>Save WiFi</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Clock</h3>";
  html += "<form action='/saveTime' method='POST'>";
  html += "UTC Offset Hours:<br>";
  html += "<input name='offsetH' type='number' min='-12' max='14' value='" + String(clockUtcOffsetMinutes / 60) + "'>";
  html += "UTC Offset Minutes:<br>";
  html += "<input name='offsetM' type='number' min='0' max='59' value='" + String(abs(clockUtcOffsetMinutes % 60)) + "'>";
  html += "<label><input name='format24' type='checkbox' style='width:auto;margin-right:8px;' ";
  if (clockUse24Hour) html += "checked";
  html += ">Use 24-hour time</label>";
  html += "<div class='small'>Current setting: " + getClockOffsetLabel() + "</div>";
  html += "<button type='submit'>Save Clock</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Saved Printers</h3>";

  if (printerCount == 0) {
    html += "<p>No printers saved yet.</p>";
  }
  else {
    for (int i = 0; i < printerCount; i++) {
      html += "<div class='card'>";
      html += "<b>" + printers[i].name + "</b>";
      if (i == selectedPrinter) html += " <span style='color:#00ff90'>(selected)</span>";
      html += "<div class='small'>IP: " + printers[i].ip + "</div>";
      html += "<div class='small'>Serial: " + printers[i].serial + "</div>";
      html += "<a class='btn select' href='/selectPrinter?i=" + String(i) + "'>Select</a>";
      html += "<a class='btn danger' href='/deletePrinter?i=" + String(i) + "' onclick='return confirm(\"Delete this printer?\")'>Delete</a>";
      html += "</div>";
    }
  }

  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Add Printer</h3>";
  html += "<form action='/addPrinter' method='POST'>";
  html += "Printer Name:<br>";
  html += "<input name='name' placeholder='P1S, X1C, A1 Mini' required>";
  html += "Printer IP Address:<br>";
  html += "<input name='ip' placeholder='192.168.1.100' required>";
  html += "Access Code:<br>";
  html += "<input name='code' required>";
  html += "Printer Serial Number:<br>";
  html += "<input name='serial' required>";
  html += "<button type='submit'>Add Printer</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Maintenance</h3>";
  html += "<a class='btn' href='/requestStatus'>Request Printer Status</a>";
  html += "<a class='btn' href='/update'>OTA Firmware Update</a>";
  html += "<a class='btn' href='/reboot'>Restart HUD</a>";
  html += "</div>";

  html += "</div></body></html>";

  return html;
}

void handleRoot() {
  server.send(200, "text/html", setupPage());
}

void handleSave() {
  // Backward-compatible save route.
  wifiSSID = server.arg("ssid");
  wifiPASS = server.arg("pass");

  if (server.hasArg("ip") && server.hasArg("code") && server.hasArg("serial")) {
    if (printerCount == 0 && server.arg("ip").length() > 0) {
      printers[0].name = "Bambu Printer";
      printers[0].ip = server.arg("ip");
      printers[0].accessCode = server.arg("code");
      printers[0].serial = server.arg("serial");
      printerCount = 1;
      selectedPrinter = 0;
    }
  }

  saveSettings();

  server.send(200, "text/html", "<h2>Saved!</h2><p>ESP32 restarting...</p>");
  drawSavedScreen();

  delay(1500);
  ESP.restart();
}


void handleSaveWifi() {
  wifiSSID = server.arg("ssid");
  wifiPASS = server.arg("pass");

  saveSettings();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSaveTime() {
  int h = server.arg("offsetH").toInt();
  int m = abs(server.arg("offsetM").toInt());

  if (h < -12) h = -12;
  if (h > 14) h = 14;
  if (m > 59) m = 59;

  if (h < 0) {
    clockUtcOffsetMinutes = (h * 60) - m;
  }
  else {
    clockUtcOffsetMinutes = (h * 60) + m;
  }

  clockUse24Hour = server.hasArg("format24");
  saveSettings();

  if (WiFi.status() == WL_CONNECTED) {
    configureClock();
  }

  lastHudClockText = "__FORCE__";
  hudDrawn = false;

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleAddPrinter() {
  if (printerCount >= MAX_PRINTERS) {
    server.send(400, "text/plain", "Printer limit reached");
    return;
  }

  String name = server.arg("name");
  String ip = server.arg("ip");
  String code = server.arg("code");
  String serial = server.arg("serial");

  if (name.length() == 0 || ip.length() == 0 || code.length() == 0 || serial.length() == 0) {
    server.send(400, "text/plain", "Missing printer information");
    return;
  }

  printers[printerCount].name = name;
  printers[printerCount].ip = ip;
  printers[printerCount].accessCode = code;
  printers[printerCount].serial = serial;

  if (selectedPrinter < 0) selectedPrinter = printerCount;

  printerCount++;
  saveSettings();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleDeletePrinter() {
  int index = server.arg("i").toInt();

  if (index < 0 || index >= printerCount) {
    server.send(400, "text/plain", "Invalid printer index");
    return;
  }

  bool deletingSelected = (index == selectedPrinter);

  for (int i = index; i < printerCount - 1; i++) {
    printers[i] = printers[i + 1];
  }

  printerCount--;

  if (printerCount <= 0) {
    printerCount = 0;
    selectedPrinter = -1;
  }
  else if (deletingSelected) {
    selectedPrinter = 0;
  }
  else if (selectedPrinter > index) {
    selectedPrinter--;
  }

  saveSettings();

  if (mqtt.connected()) mqtt.disconnect();
  syncSelectedPrinter();
  resetPrinterRuntimeData();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSelectPrinter() {
  int index = server.arg("i").toInt();

  if (index < 0 || index >= printerCount) {
    server.send(400, "text/plain", "Invalid printer index");
    return;
  }

  selectedPrinter = index;
  saveSettings();
  syncSelectedPrinter();

  if (mqtt.connected()) mqtt.disconnect();

  resetPrinterRuntimeData();
  hudDrawn = false;

  connectMQTT();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}







void handleRequestStatus() {
  Serial.println("Manual full status requested from web portal");

  if (!mqtt.connected()) {
    Serial.println("MQTT was disconnected. Reconnecting before request...");
    connectMQTT();
  }

  if (mqtt.connected()) {
    requestFullStatus();
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  }
  else {
    server.send(200, "text/html", "<h2>MQTT not connected</h2><p>Unable to request full status.</p><a href='/'>Back</a>");
  }
}


void handleReboot() {
  server.send(200, "text/html", "<h2>Restarting...</h2>");
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void startSetupPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("BambuHUD_Setup", "bambuhud");

  delay(500);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  drawSetupScreen();

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/saveWifi", HTTP_POST, handleSaveWifi);
  server.on("/saveTime", HTTP_POST, handleSaveTime);
  server.on("/addPrinter", HTTP_POST, handleAddPrinter);
  server.on("/deletePrinter", HTTP_GET, handleDeletePrinter);
  server.on("/selectPrinter", HTTP_GET, handleSelectPrinter);
  server.on("/requestStatus", HTTP_GET, handleRequestStatus);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.onNotFound(handleNotFound);

  server.begin();
}

// =====================
// WiFi
// =====================
bool connectWiFi() {
  drawNormalScreen("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());

  int retries = 0;

  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("WiFi Connected", 20, 60);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(WiFi.localIP().toString(), 20, 100);

    return true;
  }

  drawNormalScreen("WiFi Failed");
  return false;
}


// =====================
// MQTT Helpers
// =====================
int readFanPercent(JsonVariant value) {
  if (value.isNull()) return -1;

  int v = 0;

  if (value.is<int>()) {
    v = value.as<int>();
  }
  else if (value.is<const char*>()) {
    String s = value.as<String>();
    s.replace("%", "");
    s.trim();
    v = s.toInt();
  }
  else {
    return -1;
  }

  if (v < 0) v = 0;

  // Bambu P1/X1 fan gear scale: 0-15, where 15 = 100%.
  if (v <= 15) {
    return map(v, 0, 15, 0, 100);
  }

  // Some firmware/fields may report 0-255 PWM.
  if (v <= 255) {
    return map(v, 0, 255, 0, 100);
  }

  // Safety clamp for unexpected values.
  if (v > 100) v = 100;

  return v;
}

void setActiveAMSSlot(int rawSlot) {
  // Bambu sends 255 when no AMS tray is currently active/known.
  if (rawSlot == 255 || rawSlot < 0) return;

  int activeSlot = rawSlot + 1;  // Bambu tray values are zero-based.

  if (activeSlot < 1 || activeSlot > 4) return;

  for (int i = 0; i < 4; i++) {
    amsLoaded[i] = false;
    amsType[i] = "";
    // Keep any known color if we ever receive full AMS tray data later.
  }

  amsLoaded[activeSlot - 1] = true;
  amsType[activeSlot - 1] = "ACT";

  amsSummary = "Active Slot " + String(activeSlot);
  Serial.println("AMS: " + amsSummary);
}

void parseAMS(JsonObject print) {
  if (!print.containsKey("ams")) return;

  JsonObject amsObj = print["ams"];

  String summary = "";

  // Some Bambu status packets only report the active/target/current AMS tray,
  // not the full spool list. Bambu tray values appear to be zero-based:
  // tray_tar = 2 means AMS slot 3. tray_now = 255 means unknown/no active tray.
  if (amsObj.containsKey("tray_tar")) {
    setActiveAMSSlot(amsObj["tray_tar"].as<int>());
    return;
  }

  if (amsObj.containsKey("tray_pre")) {
    setActiveAMSSlot(amsObj["tray_pre"].as<int>());
    return;
  }

  if (amsObj.containsKey("tray_now")) {
    setActiveAMSSlot(amsObj["tray_now"].as<int>());
    return;
  }

  // Only clear and rebuild all slots when the printer actually sends the full AMS tray list.
  if (amsObj.containsKey("ams")) {
    for (int i = 0; i < 4; i++) {
      amsType[i] = "";
      amsColor[i] = "";
      amsLoaded[i] = false;
    }

    JsonArray amsArray = amsObj["ams"].as<JsonArray>();

    for (JsonObject amsUnit : amsArray) {
      if (!amsUnit.containsKey("tray")) continue;

      JsonArray trays = amsUnit["tray"].as<JsonArray>();

      for (JsonObject tray : trays) {
        int slot = -1;

        if (tray.containsKey("id")) {
          slot = tray["id"].as<int>();
        }
        else if (tray.containsKey("tray_id")) {
          slot = tray["tray_id"].as<int>();
        }

        if (slot >= 1 && slot <= 4) slot = slot - 1;
        if (slot < 0 || slot > 3) continue;

        String type = "";

        if (tray.containsKey("tray_type")) {
          type = tray["tray_type"].as<String>();
        }
        else if (tray.containsKey("tray_sub_brands")) {
          type = tray["tray_sub_brands"].as<String>();
        }
        else if (tray.containsKey("tray_info_idx")) {
          type = tray["tray_info_idx"].as<String>();
        }

        String color = "";

        if (tray.containsKey("tray_color")) {
          color = tray["tray_color"].as<String>();
        }
        else if (tray.containsKey("color")) {
          color = tray["color"].as<String>();
        }

        if (type.length() == 0 || type == "null") {
          type = "Loaded";
        }

        amsType[slot] = type;
        amsColor[slot] = color;
        amsLoaded[slot] = true;
      }
    }
  }

  for (int i = 0; i < 4; i++) {
    if (summary.length() > 0) summary += " | ";

    if (amsLoaded[i]) {
      summary += String(i + 1) + ":" + amsType[i];
    }
    else {
      summary += String(i + 1) + ":Empty";
    }
  }

  if (summary.length() > 0) {
    amsSummary = summary;

    if (amsSummary != lastAmsSummary) {
      Serial.println("AMS: " + amsSummary);

      for (int i = 0; i < 4; i++) {
        Serial.print("AMS Slot ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(amsLoaded[i] ? amsType[i] : "Empty");

        if (amsColor[i].length() > 0) {
          Serial.print(" Color=");
          Serial.print(amsColor[i]);
        }

        Serial.println();
      }

      // Do not update lastAmsSummary here; updateHUDValues() uses it
      // to know when the screen needs to redraw AMS.
    }
  }
}



// =====================
// Print State Helpers
// =====================
String normalizePrinterState(String rawState) {
  rawState.trim();
  rawState.toUpperCase();

  if (rawState == "RUNNING" || rawState == "PRINTING") return "Printing";
  if (rawState == "PREPARE" || rawState == "PREPARING") return "Preparing";
  if (rawState == "PAUSE" || rawState == "PAUSED") return "Paused";
  if (rawState == "FINISH" || rawState == "FINISHED" || rawState == "COMPLETE" || rawState == "COMPLETED") return "Complete";
  if (rawState == "IDLE") {
    // If the printer reports IDLE right after a job ends, keep Complete visible
    // until the 2-minute auto-clear runs.
    if (printCompletionPendingReset) return "Complete";
    return "Ready";
  }
  if (rawState == "FAILED") return "Failed";
  if (rawState == "SLICING") return "Slicing";

  // Fallback: show the raw state with nicer casing.
  rawState.toLowerCase();
  if (rawState.length() > 0) {
    rawState.setCharAt(0, toupper(rawState.charAt(0)));
  }
  return rawState;
}

void markPrintComplete() {
  progress = 100;
  remainingMinutes = 0;
  printerState = "Complete";

  if (!printCompletionPendingReset) {
    printCompletedAt = millis();
    printCompletionPendingReset = true;
    Serial.println("Print complete detected. Screen data will clear in 2 minutes.");
  }

  lastProgress = -1;
  lastRemainingMinutes = -1;
  lastPrinterState = "__FORCE__";
  lastEtaText = "__FORCE__";
}

void clearCompletedPrintData() {
  progress = 0;
  layerNum = 0;
  totalLayers = 0;
  remainingMinutes = 0;
  etaText = "--";
  fileName = "";
  displayedFileName = "";
  printerState = "Ready";

  printCompletedAt = 0;
  printCompletionPendingReset = false;

  // Force the dashboard to repaint the cleared print fields.
  lastProgress = -1;
  lastRemainingMinutes = -1;
  lastLayerNum = -1;
  lastPrinterState = "__FORCE__";
  lastDisplayedFileName = "__FORCE__";
  lastEtaText = "__FORCE__";
  hudDrawn = false;

  Serial.println("Completed print data cleared from HUD.");
}

void updateCompletedPrintReset() {
  if (!printCompletionPendingReset) return;

  if (millis() - printCompletedAt >= PRINT_COMPLETE_RESET_DELAY) {
    clearCompletedPrintData();
  }
}

// =====================
// MQTT
// =====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON parse failed on MQTT message: ");
    Serial.println(error.c_str());
    Serial.print("Topic: ");
    Serial.println(topic);
    Serial.print("Length: ");
    Serial.println(length);
    return;
  }

  JsonObject print = doc["print"];

  if (print.containsKey("ams")) {
    Serial.println("AMS update received");
  }

  if (print.containsKey("nozzle_temper")) {
    nozzleTemp = print["nozzle_temper"];
  }

  if (print.containsKey("bed_temper")) {
    bedTemp = print["bed_temper"];
  }

  if (print.containsKey("chamber_temper")) {
    chamberTemp = print["chamber_temper"];
  }
  else if (print.containsKey("chamber_temperature")) {
    chamberTemp = print["chamber_temperature"];
  }

  if (print.containsKey("cooling_fan_speed")) {
    Serial.print("cooling_fan_speed RAW=");
    Serial.println(print["cooling_fan_speed"].as<String>());
    partFanSpeed = readFanPercent(print["cooling_fan_speed"]);
  }

  if (print.containsKey("big_fan1_speed")) {
    int rawAux = print["big_fan1_speed"].as<int>();

    Serial.print("big_fan1_speed RAW=");
    Serial.println(rawAux);

    // Aux fan on this printer reports as Bambu fan gear scale 0-15.
    auxFanSpeed = readFanPercent(print["big_fan1_speed"]);
  }
  else if (print.containsKey("aux_part_fan")) {
    int rawAux = print["aux_part_fan"].as<int>();

    Serial.print("aux_part_fan RAW=");
    Serial.println(rawAux);

    // Aux fan on this printer reports as Bambu fan gear scale 0-15.
    auxFanSpeed = readFanPercent(print["aux_part_fan"]);
  }

  if (print.containsKey("big_fan2_speed")) {
    int rawChamber = print["big_fan2_speed"].as<int>();

    Serial.print("big_fan2_speed RAW=");
    Serial.println(rawChamber);

    // Chamber fan also commonly reports as 0-255 PWM.
    chamberFanSpeed = round(rawChamber * 100.0 / 255.0);

    if (chamberFanSpeed > 100) chamberFanSpeed = 100;
    if (chamberFanSpeed < 0) chamberFanSpeed = 0;
  }
  else if (print.containsKey("chamber_fan_speed")) {
    int rawChamber = print["chamber_fan_speed"].as<int>();

    Serial.print("chamber_fan_speed RAW=");
    Serial.println(rawChamber);

    chamberFanSpeed = round(rawChamber * 100.0 / 255.0);

    if (chamberFanSpeed > 100) chamberFanSpeed = 100;
    if (chamberFanSpeed < 0) chamberFanSpeed = 0;
  }

  parseAMS(print);

  if (print.containsKey("mc_remaining_time")) {
    remainingMinutes = print["mc_remaining_time"];
  }

  if (print.containsKey("mc_percent")) {
    progress = print["mc_percent"];

    if (progress >= 97 && remainingMinutes == 0) {
      markPrintComplete();
    }
    else {
      lastProgress = -1;
    }
  }

  if (print.containsKey("gcode_state")) {
    String rawState = String((const char*)print["gcode_state"]);
    String mappedState = normalizePrinterState(rawState);

    if (mappedState == "Printing" || mappedState == "Preparing") {
      // A new active print cancels any pending completed-print cleanup.
      printCompletionPendingReset = false;
      printCompletedAt = 0;
    }

    if (mappedState == "Complete") {
      markPrintComplete();
    }
    else {
      printerState = mappedState;
      lastPrinterState = "__FORCE__";
    }
  }

  if (print.containsKey("layer_num")) {
    layerNum = print["layer_num"];
    lastLayerNum = -1;

    Serial.print("layer_num=");
    Serial.println(layerNum);
  }

  if (print.containsKey("total_layer_num")) {
    totalLayers = print["total_layer_num"];
  }

  String incomingFile = "";

  if (print.containsKey("gcode_file")) {
    incomingFile = String((const char*)print["gcode_file"]);
  }

  if (print.containsKey("subtask_name")) {
    incomingFile = String((const char*)print["subtask_name"]);
  }

  if (print.containsKey("file")) {
    incomingFile = String((const char*)print["file"]);
  }

  if (print.containsKey("task_name")) {
    incomingFile = String((const char*)print["task_name"]);
  }

  if (print.containsKey("project_name")) {
    incomingFile = String((const char*)print["project_name"]);
  }

  if (incomingFile.length() > 0) {
    fileName = incomingFile;
    displayedFileName = incomingFile;
    lastDisplayedFileName = "__FORCE__";

    Serial.println("FILE: " + displayedFileName);
  }

  if (print.containsKey("wifi_signal")) {
    wifiSignal = String((const char*)print["wifi_signal"]);
  }
}

bool connectMQTT() {
  syncSelectedPrinter();

  if (selectedPrinter < 0 || printerIP.length() == 0 || accessCode.length() == 0 || printerSerial.length() == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("No Printer Selected", 20, 60);
    tft.drawString("Open setup portal", 20, 100);
    printerState = "No Printer";
    return false;
  }

  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Connecting Printer...", 20, 60);
  tft.drawString(printerName, 20, 95);

  secureClient.setInsecure();

  mqtt.setServer(printerIP.c_str(), 8883);
  mqtt.setCallback(mqttCallback);

  String clientID = "ESP32HUD_" + String((uint32_t)ESP.getEfuseMac(), HEX);

  bool connected = mqtt.connect(
    clientID.c_str(),
    "bblp",
    accessCode.c_str()
  );

  if (connected) {
    String baseTopic = "device/" + printerSerial;
    String reportTopic = baseTopic + "/report";

    // Bambu's local MQTT broker can disconnect when wildcard subscriptions are used.
    // Keep this subscription stable and log every packet that arrives on the normal report topic.
    bool subReport = mqtt.subscribe(reportTopic.c_str());

    Serial.println("Subscribed MQTT topic:");
    Serial.print("  ");
    Serial.print(reportTopic);
    Serial.print(" -> ");
    Serial.println(subReport ? "OK" : "FAILED");

    requestFullStatus();

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Printer Connected", 20, 60);
    tft.drawString(printerName, 20, 95);

    printerState = "Connected";
    return true;
  }

  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("MQTT Failed", 20, 60);
  tft.drawString(String(mqtt.state()), 20, 100);

  printerState = "MQTT Failed";
  return false;
}

void publishPrinterRequest(JsonDocument& doc, const String& label) {
  if (selectedPrinter < 0 || printerSerial.length() == 0 || !mqtt.connected()) {
    Serial.println(label + " skipped: MQTT not connected or no printer selected");
    return;
  }

  String payload;
  serializeJson(doc, payload);

  String topic = "device/" + printerSerial + "/request";

  bool ok = mqtt.publish(topic.c_str(), payload.c_str());

  Serial.print(label);
  Serial.print(" -> ");
  Serial.println(ok ? "OK" : "FAILED");

  if (!ok) {
    Serial.print("Topic: ");
    Serial.println(topic);
    Serial.print("Payload: ");
    Serial.println(payload);
  }
}

void requestFullStatus() {
  JsonDocument doc;

  // OpenBambuAPI-style PushAll request plus AMS control.
  doc["pushing"]["sequence_id"] = String(millis());
  doc["pushing"]["command"] = "pushall";
  doc["pushing"]["version"] = 1;
  doc["pushing"]["push_target"] = 1;

  // Combined request: kept for comparison with PushAll-only.
  doc["print"]["sequence_id"] = String(millis());
  doc["print"]["command"] = "ams_control";

  publishPrinterRequest(doc, "Requested printer status");
}

// =====================
// Touch
// =====================
void handleTouch() {
  if (!ts.touched()) return;

  TS_Point p = ts.getPoint();

  if (p.x == 8191 || p.y == 8191) return;
  if (p.x < 100 || p.y < 100) return;

  // Any touch wakes the screen first. If the screen was dim/off, do not
  // also press a button on that same touch.
  if (powerSaveActive) {
    wakeDisplay();
    delay(250);
    return;
  }

  lastUserActivity = millis();

  Serial.print("TOUCH X=");
  Serial.print(p.x);
  Serial.print(" Y=");
  Serial.println(p.y);

  int screenX = map(p.x, TOUCH_MAP_X_MIN, TOUCH_MAP_X_MAX, 0, SCREEN_W);
  int screenY = map(p.y, TOUCH_MAP_Y_MIN, TOUCH_MAP_Y_MAX, 0, SCREEN_H);

  screenX = constrain(screenX, 0, SCREEN_W - 1);
  screenY = constrain(screenY, 0, SCREEN_H - 1);

  // Do NOT manually invert coordinates here. The touch controller remains
  // physically mounted the same way, and the raw-to-screen calibration below
  // already maps the actual touch position to the button layout. Manual
  // inversion caused the bottom buttons to respond from the top of the screen.

  // Bottom navigation buttons.
  if (screenY >= BOTTOM_NAV_Y) {
    int tabW = SCREEN_W / 4;
    if (screenX < tabW) {
      currentPage = PAGE_DASHBOARD;
    }
    else if (screenX < tabW * 2) {
      currentPage = PAGE_PRINTERS;
    }
    else if (screenX < tabW * 3) {
      currentPage = PAGE_SETTINGS;
    }
    else {
      currentPage = PAGE_STATS;
    }

    hudDrawn = false;
    lastPrinterState = "";
    lastWifiSignal = "";
    lastProgress = -1;
    lastNozzleTemp = -999;
    lastBedTemp = -999;
    lastLayerNum = -1;
    lastRemainingMinutes = -1;
    lastDisplayedFileName = "__FORCE__";
    lastAmsSummary = "__FORCE__";
    lastEtaText = "__FORCE__";

    delay(250);
    return;
  }

  if (currentPage == PAGE_SETTINGS) {
    if (SCREEN_W <= 320) {
      if (screenX >= 10 && screenX <= 132 && screenY >= 49 && screenY <= 75) { currentPage = PAGE_AMS_COLORS; hudDrawn = false; delay(250); return; }
      if (screenX >= 190 && screenX <= 310 && screenY >= 49 && screenY <= 75) { if (!mqtt.connected()) connectMQTT(); if (mqtt.connected()) requestFullStatus(); delay(300); return; }
      if (screenX >= 10 && screenX <= 155 && screenY >= 80 && screenY <= 106) { screenRotation = (screenRotation == 1) ? 3 : 1; prefs.begin("bambu", false); prefs.putInt("rotation", screenRotation); prefs.end(); applyScreenRotation(); hudDrawn = false; delay(400); return; }
      if (screenX >= 10 && screenX <= 215 && screenY >= 111 && screenY <= 137) { themeID++; if (themeID > 6) themeID = 0; prefs.begin("bambu", false); prefs.putInt("theme", themeID); prefs.end(); hudDrawn = false; delay(300); return; }
      if (screenX >= 10 && screenX <= 155 && screenY >= 142 && screenY <= 168) { powerSaveMode++; if (powerSaveMode > 2) powerSaveMode = 0; prefs.begin("bambu", false); prefs.putInt("ps_mode", powerSaveMode); prefs.end(); wakeDisplay(); hudDrawn = false; delay(300); return; }
      if (screenX >= 164 && screenX <= 310 && screenY >= 142 && screenY <= 168) { powerSaveTimeoutIndex++; if (powerSaveTimeoutIndex > 6) powerSaveTimeoutIndex = 0; prefs.begin("bambu", false); prefs.putInt("ps_timeout", powerSaveTimeoutIndex); prefs.end(); wakeDisplay(); hudDrawn = false; delay(300); return; }
      if (screenX >= 10 && screenX <= 155 && screenY >= 173 && screenY <= 199) { displayStyle++; if (displayStyle > 4) displayStyle = 0; prefs.begin("bambu", false); prefs.putInt("display_style", displayStyle); prefs.end(); hudDrawn = false; delay(300); return; }
    }

    // AMS Colors button on Settings page.
    if (screenX >= 5 && screenX <= 215 && screenY >= 72 && screenY <= 104) {
      currentPage = PAGE_AMS_COLORS;
      hudDrawn = false;
      delay(250);
      return;
    }

    // Rotate Display button.
    if (screenX >= 5 && screenX <= 260 && screenY >= 112 && screenY <= 144) {
      screenRotation = (screenRotation == 1) ? 3 : 1;

      prefs.begin("bambu", false);
      prefs.putInt("rotation", screenRotation);
      prefs.end();

      applyScreenRotation();

      currentPage = PAGE_SETTINGS;
      hudDrawn = false;

      Serial.print("Screen rotation set to ");
      Serial.println(screenRotation == 1 ? "Normal" : "180");

      delay(400);
      return;
    }

    // Manual printer status request button.
    if (screenX >= 300 && screenX <= 465 && screenY >= 72 && screenY <= 104) {
      Serial.println("Manual full status requested from Settings button");

      if (!mqtt.connected()) {
        Serial.println("MQTT was disconnected. Reconnecting before request...");
        connectMQTT();
      }

      if (mqtt.connected()) {
        requestFullStatus();
      }
      else {
        Serial.print("Unable to request status. MQTT state: ");
        Serial.println(mqtt.state());
      }

      delay(300);
      return;
    }


    // Theme button. Tap to cycle through saved background/accent themes.
    if (screenX >= 5 && screenX <= 330 && screenY >= 152 && screenY <= 184) {
      themeID++;
      if (themeID > 6) themeID = 0;

      prefs.begin("bambu", false);
      prefs.putInt("theme", themeID);
      prefs.end();

      currentPage = PAGE_SETTINGS;
      hudDrawn = false;

      // Force all dashboard values to repaint with the new colors.
      lastPrinterState = "";
      lastWifiSignal = "";
      lastProgress = -1;
      lastNozzleTemp = -999;
      lastBedTemp = -999;
      lastChamberTemp = -999;
      lastPartFanSpeed = -999;
      lastAuxFanSpeed = -999;
      lastLayerNum = -1;
      lastRemainingMinutes = -1;
      lastAmsSummary = "__FORCE__";
      lastEtaText = "__FORCE__";

      Serial.print("Theme set to ");
      Serial.println(getThemeName());

      delay(300);
      return;
    }

    // Font button: Classic -> Modern -> Bold -> Digital -> Compact.
    if (screenX >= 5 && screenX <= 282 && screenY >= 232 && screenY <= 264) {
      displayStyle++;
      if (displayStyle > 4) displayStyle = 0;

      prefs.begin("bambu", false);
      prefs.putInt("display_style", displayStyle);
      prefs.end();

      hudDrawn = false;
      lastPrinterState = "";
      lastWifiSignal = "";
      lastProgress = -1;
      lastNozzleTemp = -999;
      lastBedTemp = -999;
      lastChamberTemp = -999;
      lastPartFanSpeed = -999;
      lastAuxFanSpeed = -999;
      lastLayerNum = -1;
      lastRemainingMinutes = -1;
      lastAmsSummary = "__FORCE__";
      lastEtaText = "__FORCE__";

      Serial.print("Font style set to ");
      Serial.println(getDisplayStyleName());

      delay(300);
      return;
    }

    // Power Save mode button: Disabled -> Dim -> Off.
    if (screenX >= 5 && screenX <= 230 && screenY >= 192 && screenY <= 224) {
      powerSaveMode++;
      if (powerSaveMode > 2) powerSaveMode = 0;

      prefs.begin("bambu", false);
      prefs.putInt("ps_mode", powerSaveMode);
      prefs.end();

      wakeDisplay();
      hudDrawn = false;

      Serial.print("Power save mode set to ");
      Serial.println(getPowerSaveModeLabel());

      delay(300);
      return;
    }

    // Timeout button: Never, 1, 5, 10, 15, 30, 60 minutes.
    if (screenX >= 245 && screenX <= 465 && screenY >= 192 && screenY <= 224) {
      powerSaveTimeoutIndex++;
      if (powerSaveTimeoutIndex > 6) powerSaveTimeoutIndex = 0;

      prefs.begin("bambu", false);
      prefs.putInt("ps_timeout", powerSaveTimeoutIndex);
      prefs.end();

      wakeDisplay();
      hudDrawn = false;

      Serial.print("Power save timeout set to ");
      Serial.println(getPowerSaveTimeoutLabel());

      delay(300);
      return;
    }
  }

  if (currentPage == PAGE_AMS_COLORS) {
    if (SCREEN_W <= 320) {
      if (screenY >= 55 && screenY <= 95) {
        int slot = (screenX - 14) / 76;
        if (slot >= 0 && slot < 4) { selectedAmsColorSlot = slot; hudDrawn = false; delay(200); return; }
      }
      if (screenY >= 108 && screenY <= 205) {
        int col = (screenX - 16) / 76;
        int row = (screenY - 112) / 30;
        if (col >= 0 && col < 4 && row >= 0 && row < 3) {
          int colorIndex = row * 4 + col;
          String colors[12] = {
            "#000000", "#FFFFFF", "#FF0000", "#0000FF",
            "#00FF00", "#FFFF00", "#FF8000", "#8000FF",
            "#808080", "#FF66CC", "#8B4513", "#222222"
          };
          manualAmsColor[selectedAmsColorSlot] = colors[colorIndex];
          saveSettings();
          lastAmsSummary = "__FORCE__";
          hudDrawn = false;
          delay(250);
          return;
        }
      }
    }

    // Slot selector row.
    if (screenY >= 60 && screenY <= 125) {
      int slot = screenX / 115;
      if (slot >= 0 && slot < 4) {
        selectedAmsColorSlot = slot;
        hudDrawn = false;
        delay(200);
        return;
      }
    }

    // Color button grid.
    if (screenY >= 135 && screenY <= 270) {
      int col = (screenX - 15) / 115;
      int row = (screenY - 135) / 45;

      if (col >= 0 && col < 4 && row >= 0 && row < 3) {
        int colorIndex = row * 4 + col;

        String colors[12] = {
          "#000000", "#FFFFFF", "#FF0000", "#0000FF",
          "#00FF00", "#FFFF00", "#FF8000", "#8000FF",
          "#808080", "#FF66CC", "#8B4513", "#222222"
        };

        manualAmsColor[selectedAmsColorSlot] = colors[colorIndex];
        saveSettings();

        // Force AMS/HUD redraw.
        lastAmsSummary = "__FORCE__";
        hudDrawn = false;

        Serial.print("Manual AMS Slot ");
        Serial.print(selectedAmsColorSlot + 1);
        Serial.print(" Color=");
        Serial.println(manualAmsColor[selectedAmsColorSlot]);

        delay(250);
        return;
      }
    }
  }

  // Printer selection rows on the Printers page.
  if (currentPage == PAGE_PRINTERS) {
    if (SCREEN_W <= 320 && screenY >= 50 && screenY <= 185) {
      int index = (screenY - 58) / 32;

      if (index >= 0 && index < printerCount) {
        selectedPrinter = index;
        saveSettings();
        syncSelectedPrinter();
        if (mqtt.connected()) mqtt.disconnect();
        resetPrinterRuntimeData();
        hudDrawn = false;
        connectMQTT();
        currentPage = PAGE_DASHBOARD;
        hudDrawn = false;
        delay(400);
        return;
      }
    }

    if (screenY >= 70 && screenY <= 245) {
      int index = (screenY - 70) / 35;

      if (index >= 0 && index < printerCount) {
        selectedPrinter = index;
        saveSettings();
        syncSelectedPrinter();

        if (mqtt.connected()) mqtt.disconnect();

        resetPrinterRuntimeData();
        hudDrawn = false;

        connectMQTT();

        delay(500);
        if (mqtt.connected()) {
          requestFullStatus();
          Serial.println("Manual full status requested from printer tap");
        }
        else {
          Serial.print("Printer tap reconnect failed. MQTT state: ");
          Serial.println(mqtt.state());
        }

        currentPage = PAGE_DASHBOARD;
        hudDrawn = false;

        delay(400);
        return;
      }
    }
  }

  delay(150);
}

// =====================
// Display
// =====================

// =====================
// Theme Helpers
// =====================
String getThemeName() {
  switch (themeID) {
    case 1: return "Bambu Blue";
    case 2: return "Carbon";
    case 3: return "Matrix";
    case 4: return "OLED Black";
    case 5: return "Red Racing";
    case 6: return "Orange";
    default: return "Bambu Green";
  }
}

String getDisplayStyleName() {
  switch (displayStyle) {
    case 1: return "Modern";
    case 2: return "Bold";
    case 3: return "Digital";
    case 4: return "Compact";
    default: return "Classic";
  }
}

uint16_t themeBgTop() {
  switch (themeID) {
    case 1: return tft.color565(0, 10, 24);     // Blue
    case 2: return tft.color565(18, 18, 18);    // Carbon
    case 3: return tft.color565(0, 18, 0);      // Matrix
    case 4: return tft.color565(0, 0, 0);       // OLED
    case 5: return tft.color565(24, 0, 0);      // Red
    case 6: return tft.color565(28, 10, 0);     // Orange
    default: return tft.color565(2, 12, 9);     // Green
  }
}

uint16_t themeBgBottom() {
  switch (themeID) {
    case 1: return tft.color565(0, 0, 8);
    case 2: return tft.color565(0, 0, 0);
    case 3: return tft.color565(0, 0, 0);
    case 4: return tft.color565(0, 0, 0);
    case 5: return tft.color565(0, 0, 0);
    case 6: return tft.color565(0, 0, 0);
    default: return tft.color565(0, 0, 0);
  }
}

uint16_t themePanel() {
  switch (themeID) {
    case 1: return tft.color565(5, 18, 36);
    case 2: return tft.color565(24, 24, 24);
    case 3: return tft.color565(3, 20, 5);
    case 4: return tft.color565(0, 0, 0);
    case 5: return tft.color565(28, 8, 8);
    case 6: return tft.color565(28, 14, 3);
    default: return tft.color565(8, 24, 18);
  }
}

uint16_t themePanelDark() {
  switch (themeID) {
    case 1: return tft.color565(0, 7, 18);
    case 2: return tft.color565(8, 8, 8);
    case 3: return tft.color565(0, 8, 2);
    case 4: return tft.color565(0, 0, 0);
    case 5: return tft.color565(10, 0, 0);
    case 6: return tft.color565(10, 4, 0);
    default: return tft.color565(3, 12, 10);
  }
}

uint16_t themeGreen() {
  switch (themeID) {
    case 1: return tft.color565(0, 165, 255);
    case 2: return tft.color565(0, 190, 115);
    case 3: return tft.color565(0, 255, 80);
    case 4: return tft.color565(0, 210, 120);
    case 5: return tft.color565(255, 55, 55);
    case 6: return tft.color565(255, 145, 0);
    default: return tft.color565(0, 190, 115);
  }
}

uint16_t themeGreenSoft() {
  switch (themeID) {
    case 1: return tft.color565(0, 70, 120);
    case 2: return tft.color565(65, 65, 65);
    case 3: return tft.color565(0, 80, 25);
    case 4: return tft.color565(0, 70, 40);
    case 5: return tft.color565(115, 20, 20);
    case 6: return tft.color565(120, 55, 0);
    default: return tft.color565(0, 95, 60);
  }
}

uint16_t themeLine() {
  switch (themeID) {
    case 1: return tft.color565(20, 75, 120);
    case 2: return tft.color565(80, 80, 80);
    case 3: return tft.color565(0, 90, 30);
    case 4: return tft.color565(35, 35, 35);
    case 5: return tft.color565(120, 25, 25);
    case 6: return tft.color565(130, 65, 5);
    default: return tft.color565(18, 75, 50);
  }
}

uint16_t themeTextMuted() {
  switch (themeID) {
    case 1: return tft.color565(150, 195, 220);
    case 2: return tft.color565(190, 190, 190);
    case 3: return tft.color565(120, 230, 140);
    case 4: return tft.color565(180, 180, 180);
    case 5: return tft.color565(230, 170, 170);
    case 6: return tft.color565(235, 190, 130);
    default: return tft.color565(155, 190, 170);
  }
}

uint16_t blend565(uint8_t r1, uint8_t g1, uint8_t b1,
                  uint8_t r2, uint8_t g2, uint8_t b2,
                  int step, int total) {
  if (total <= 0) total = 1;
  uint8_t r = r1 + ((int)(r2 - r1) * step) / total;
  uint8_t g = g1 + ((int)(g2 - g1) * step) / total;
  uint8_t b = b1 + ((int)(b2 - b1) * step) / total;
  return tft.color565(r, g, b);
}

void drawGradientBackground() {
  uint8_t r1, g1, b1, r2, g2, b2;

  switch (themeID) {
    case 1: r1 = 0;  g1 = 5;  b1 = 22; r2 = 0; g2 = 22; b2 = 42; break;
    case 2: r1 = 4;  g1 = 4;  b1 = 4;  r2 = 25; g2 = 25; b2 = 25; break;
    case 3: r1 = 0;  g1 = 0;  b1 = 0;  r2 = 0; g2 = 35; b2 = 8;  break;
    case 4: r1 = 0;  g1 = 0;  b1 = 0;  r2 = 0; g2 = 0;  b2 = 0;  break;
    case 5: r1 = 0;  g1 = 0;  b1 = 0;  r2 = 36; g2 = 0;  b2 = 0;  break;
    case 6: r1 = 0;  g1 = 0;  b1 = 0;  r2 = 38; g2 = 15; b2 = 0;  break;
    default:r1 = 0;  g1 = 0;  b1 = 0;  r2 = 2; g2 = 28; b2 = 18; break;
  }

  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t c = blend565(r1, g1, b1, r2, g2, b2, y, SCREEN_H - 1);
    tft.drawFastHLine(0, y, SCREEN_W, c);
  }
}

void drawFeather(int x, int y, int len, bool flip) {
  int dir = flip ? -1 : 1;

  uint16_t shadow = themePanelDark();
  uint16_t high = themeLine();

  for (int pass = 0; pass < 2; pass++) {
    int ox = pass == 0 ? 2 : 0;
    int oy = pass == 0 ? 2 : 0;
    uint16_t col = pass == 0 ? shadow : high;

    tft.drawLine(x + ox, y + oy, x + ox + dir * len, y + oy + len / 2, col);

    for (int i = 8; i < len; i += 10) {
      int sx = x + ox + dir * i;
      int sy = y + oy + i / 2;
      tft.drawLine(sx, sy, sx - dir * 18, sy - 8, col);
      tft.drawLine(sx, sy, sx - dir * 14, sy + 9, col);
    }
  }
}

void drawEmbossedFeathers() {
  // Keep OLED theme cleaner and keep the compact CYD screen uncluttered.
  if (themeID == 4 || SCREEN_W <= 320) return;

  drawFeather(365, 24, 90, false);
  drawFeather(455, 122, 72, true);
  drawFeather(380, 218, 82, false);
  drawFeather(45, 110, 70, false);
}

void drawCard(int x, int y, int w, int h) {
  tft.fillRoundRect(x + 2, y + 2, w, h, 8, TFT_BLACK);
  tft.fillRoundRect(x, y, w, h, 8, themePanel());
  tft.drawRoundRect(x, y, w, h, 8, themeLine());
}

void drawSmallLabel(String text, int x, int y) {
  useLabelFont();
  tft.setTextColor(themeTextMuted(), themePanel());
  tft.drawString(text, x, y);
}

void drawBottomButtons() {
  int navY = BOTTOM_NAV_Y;
  int navH = SCREEN_H - navY - 3;
  int navX = 6;
  int navW = SCREEN_W - 12;
  int tabW = navW / 4;

  tft.fillRoundRect(navX, navY, navW, navH, 8, themePanelDark());
  tft.drawRoundRect(navX, navY, navW, navH, 8, themeLine());

  useClassicFont(SCREEN_W <= 320 ? 1 : 2);

  uint16_t hudColor = currentPage == PAGE_DASHBOARD ? themeGreen() : TFT_WHITE;
  uint16_t printerColor = currentPage == PAGE_PRINTERS ? themeGreen() : TFT_WHITE;
  uint16_t settingsColor = currentPage == PAGE_SETTINGS || currentPage == PAGE_AMS_COLORS ? themeGreen() : TFT_WHITE;
  uint16_t statsColor = currentPage == PAGE_STATS ? themeGreen() : TFT_WHITE;

  int textY = navY + (SCREEN_W <= 320 ? 9 : 9);

  tft.setTextColor(hudColor, themePanelDark());
  tft.drawCentreString("HUD", navX + tabW / 2, textY, 1);

  tft.setTextColor(printerColor, themePanelDark());
  tft.drawCentreString("Print", navX + tabW + tabW / 2, textY, 1);

  tft.setTextColor(settingsColor, themePanelDark());
  tft.drawCentreString("Set", navX + tabW * 2 + tabW / 2, textY, 1);

  tft.setTextColor(statsColor, themePanelDark());
  tft.drawCentreString("Stats", navX + tabW * 3 + tabW / 2, textY, 1);

  tft.drawFastVLine(navX + tabW, navY + 4, navH - 8, themeLine());
  tft.drawFastVLine(navX + tabW * 2, navY + 4, navH - 8, themeLine());
  tft.drawFastVLine(navX + tabW * 3, navY + 4, navH - 8, themeLine());
}
uint16_t amsColorToTFT(String color) {
  color.replace("#", "");
  color.trim();

  if (color.length() < 6) return TFT_DARKGREY;

  long rgb = strtol(color.substring(0, 6).c_str(), NULL, 16);

  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;

  return tft.color565(r, g, b);
}

String getAMSDisplayColor(int slot) {
  if (slot < 0 || slot > 3) return "#222222";

  if (manualAmsColor[slot].length() >= 6) {
    return manualAmsColor[slot];
  }

  if (amsColor[slot].length() >= 6) {
    return amsColor[slot];
  }

  return "#222222";
}

String fitText(String text, int maxPx) {
  if (maxPx <= 0) return "";
  while (text.length() > 0 && tft.textWidth(text) > maxPx) {
    text.remove(text.length() - 1);
  }
  return text;
}

void drawFittedString(String text, int x, int y, int maxPx, uint16_t fg, uint16_t bg) {
  tft.setTextColor(fg, bg);
  String out = text;
  if (tft.textWidth(out) > maxPx) {
    while (out.length() > 0 && tft.textWidth(out + "...") > maxPx) {
      out.remove(out.length() - 1);
    }
    out += "...";
  }
  tft.drawString(out, x, y);
}

void drawRightFittedString(String text, int rightX, int y, int maxPx, uint16_t fg, uint16_t bg) {
  tft.setTextColor(fg, bg);
  String out = fitText(text, maxPx);
  int w = tft.textWidth(out);
  tft.drawString(out, rightX - w, y);
}

void drawAMSLine() {
  // Dedicated AMS card. Numbers/X are drawn inside each capsule so
  // nothing hangs below the slot indicators.
  tft.fillRoundRect(72, 238, 392, 38, 7, themePanel());
  tft.drawRoundRect(72, 238, 392, 38, 7, themeLine());

  for (int i = 0; i < 4; i++) {
    int x = 108 + (i * 88);
    int y = 247;
    int w = 58;
    int h = 20;

    bool hasManualColor = (manualAmsColor[i].length() >= 6 && manualAmsColor[i] != "#222222");
    bool showSlot = amsLoaded[i] || hasManualColor;

    if (showSlot) {
      uint16_t boxColor = amsColorToTFT(getAMSDisplayColor(i));

      tft.fillRoundRect(x, y, w, h, 7, boxColor);

      // Active AMS slot: draw a thicker theme-colored border so it is
      // easier to see which spool is currently being used.
      if (amsLoaded[i]) {
        tft.drawRoundRect(x, y, w, h, 7, themeGreen());
        tft.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 8, themeGreen());
        tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 9, themeGreen());
      }
      else {
        tft.drawRoundRect(x, y, w, h, 7, TFT_WHITE);
      }

      uint16_t textColor = TFT_WHITE;
      String c = getAMSDisplayColor(i);
      c.toUpperCase();
      if (c == "#FFFFFF" || c == "#FFFF00" || c == "#CCCCCC") {
        textColor = TFT_BLACK;
      }

      tft.setTextSize(2);
      tft.setTextColor(textColor, boxColor);
      tft.drawCentreString(String(i + 1), x + (w / 2), y + 2, 1);
    }
    else {
      tft.fillRoundRect(x, y, w, h, 7, themePanelDark());
      tft.drawRoundRect(x, y, w, h, 7, TFT_RED);

      tft.setTextSize(2);
      tft.setTextColor(TFT_RED, themePanelDark());
      tft.drawCentreString("X", x + (w / 2), y + 2, 1);
    }
  }

  tft.setTextSize(2);
}
void drawAMSLineCompact(int yBase) {
  int cardX = 6;
  int cardW = SCREEN_W - 12;
  int slotW = 46;
  int gap = 8;
  int startX = cardX + 58;

  drawCard(cardX, yBase, cardW, 32);
  useClassicFont(1);
  tft.setTextColor(themeTextMuted(), themePanel());
  tft.drawString("AMS", cardX + 10, yBase + 11);

  for (int i = 0; i < 4; i++) {
    int x = startX + i * (slotW + gap);
    int y = yBase + 7;
    bool hasManualColor = (manualAmsColor[i].length() >= 6 && manualAmsColor[i] != "#222222");
    bool showSlot = amsLoaded[i] || hasManualColor;

    if (showSlot) {
      uint16_t boxColor = amsColorToTFT(getAMSDisplayColor(i));
      tft.fillRoundRect(x, y, slotW, 18, 6, boxColor);
      tft.drawRoundRect(x, y, slotW, 18, 6, amsLoaded[i] ? themeGreen() : TFT_WHITE);
      String c = getAMSDisplayColor(i);
      c.toUpperCase();
      uint16_t textColor = (c == "#FFFFFF" || c == "#FFFF00" || c == "#CCCCCC") ? TFT_BLACK : TFT_WHITE;
      tft.setTextColor(textColor, boxColor);
      useClassicFont(1);
      tft.drawCentreString(String(i + 1), x + slotW / 2, y + 5, 1);
    }
    else {
      tft.fillRoundRect(x, y, slotW, 18, 6, themePanelDark());
      tft.drawRoundRect(x, y, slotW, 18, 6, TFT_RED);
      tft.setTextColor(TFT_RED, themePanelDark());
      useClassicFont(1);
      tft.drawCentreString("X", x + slotW / 2, y + 5, 1);
    }
  }
}

void drawHUDLayoutCompact() {
  drawGradientBackground();

  // Header
  drawCard(4, 4, SCREEN_W - 8, 28);
  useClassicFont(1);
  String title = printerName.length() ? printerName : "Bambu HUD";
  drawFittedString(title, 12, 13, 105, themeGreen(), themePanel());

  updateClockText();
  tft.fillRect(222, 9, 86, 18, themePanel());
  tft.setTextColor(themeGreen(), themePanel());
  tft.drawRightString(hudClockText, 306, 13, 1);

  // Status and progress
  drawCard(4, 38, SCREEN_W - 8, 76);
  useClassicFont(1);
  tft.setTextColor(themeTextMuted(), themePanel());
  tft.drawString("STATUS", 14, 48);
  tft.drawString("LEFT", 214, 48);
  tft.drawString("PROGRESS", 14, 76);
  tft.drawString("ETA", 214, 76);
  tft.drawRoundRect(14, 100, 292, 9, 4, themeGreenSoft());

  // Temp and fan cards
  drawCard(4, 118, 151, 56);
  drawCard(165, 118, 151, 56);
  useClassicFont(1);
  tft.setTextColor(themeTextMuted(), themePanel());
  tft.drawString("NOZZLE", 14, 129);
  tft.drawString("BED", 14, 151);
  tft.drawString("FAN", 176, 129);
  tft.drawString("AUX", 176, 151);

  drawAMSLineCompact(177);
  drawBottomButtons();
}

void updateHUDValuesCompact() {
  if (millis() - lastClockRead > 1000 || lastClockRead == 0) {
    lastClockRead = millis();
    updateClockText();
  }

  if (hudClockText != lastHudClockText) {
    tft.fillRect(222, 9, 86, 18, themePanel());
    useClassicFont(1);
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawRightString(hudClockText, 306, 13, 1);
    lastHudClockText = hudClockText;
  }

  if (printerState != lastPrinterState) {
    tft.fillRect(70, 47, 120, 17, themePanel());
    useClassicFont(1);
    drawFittedString(printerState, 70, 48, 115, TFT_WHITE, themePanel());
    lastPrinterState = printerState;
  }

  if (remainingMinutes != lastRemainingMinutes) {
    int h = remainingMinutes / 60;
    int m = remainingMinutes % 60;
    String remainingText = remainingMinutes <= 0 ? "--" : String(h) + "h " + String(m) + "m";
    tft.fillRect(252, 47, 55, 17, themePanel());
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawRightString(remainingText, 306, 48, 1);

    etaText = getETAText();
    tft.fillRect(244, 75, 63, 17, themePanel());
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawRightString(etaText, 306, 76, 1);
    lastEtaText = etaText;
    lastRemainingMinutes = remainingMinutes;
  }

  if (progress != lastProgress) {
    tft.fillRect(90, 75, 55, 17, themePanel());
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawString(String(progress) + "%", 90, 76);
    tft.fillRoundRect(16, 102, 288, 5, 3, themePanelDark());
    int barWidth = map(progress, 0, 100, 0, 288);
    tft.fillRoundRect(16, 102, barWidth, 5, 3, themeGreen());
    lastProgress = progress;
  }

  if (nozzleTemp != lastNozzleTemp) {
    tft.fillRect(83, 127, 65, 17, themePanel());
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawRightString(String(nozzleTemp, 0) + "C", 148, 129, 1);
    lastNozzleTemp = nozzleTemp;
  }

  if (bedTemp != lastBedTemp) {
    tft.fillRect(83, 149, 65, 17, themePanel());
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawRightString(String(bedTemp, 0) + "C", 148, 151, 1);
    lastBedTemp = bedTemp;
  }

  if (partFanSpeed != lastPartFanSpeed) {
    tft.fillRect(235, 127, 72, 17, themePanel());
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());
    String txt = partFanSpeed >= 0 ? String(partFanSpeed) + "%" : "--";
    tft.drawRightString(txt, 306, 129, 1);
    lastPartFanSpeed = partFanSpeed;
  }

  if (auxFanSpeed != lastAuxFanSpeed) {
    tft.fillRect(235, 149, 72, 17, themePanel());
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());
    String txt = auxFanSpeed >= 0 ? String(auxFanSpeed) + "%" : "--";
    tft.drawRightString(txt, 306, 151, 1);
    lastAuxFanSpeed = auxFanSpeed;
  }

  if (amsSummary != lastAmsSummary) {
    drawAMSLineCompact(177);
    lastAmsSummary = amsSummary;
  }

  if (layerNum != lastLayerNum) {
    lastLayerNum = layerNum;
  }

  lastDisplayedFileName = displayedFileName;
}

void drawHUDLayout() {
  if (SCREEN_W <= 320) {
    drawHUDLayoutCompact();
    return;
  }

  drawGradientBackground();
  drawEmbossedFeathers();

  // Header
  drawCard(6, 6, 468, 32);
  useHeaderFont();
  tft.setTextColor(themeGreen(), themePanel());

  if (printerName.length() > 0) {
    String title = printerName;
    if (title.length() > 13) title = title.substring(0, 13);
    tft.drawString(title, 18, 14);
  }
  else {
    tft.drawString("Bambu HUD", 18, 14);
  }

  updateClockText();
  drawClockIndicator();

  // Draw battery once as part of the static header so it appears immediately.
  updateBatteryStatus();
  drawBatteryIndicator();

  // Status / progress / remaining card
  drawCard(6, 45, 468, 96);
  drawSmallLabel("Status:", 18, 56);
  drawSmallLabel("Time:", 310, 56);
  drawSmallLabel("ETA:", 310, 86);
  drawSmallLabel("Progress:", 18, 86);
  tft.drawRoundRect(18, 115, 444, 18, 8, themeGreenSoft());

  // Metrics cards with enough height so text stays inside each panel.
  drawCard(6, 144, 224, 88);
  drawCard(244, 144, 230, 88);

  // Same proven layout for every font theme.
  drawSmallLabel("Nozzle:", 18, 154);
  drawSmallLabel("Bed:", 18, 180);
  drawSmallLabel("Layer:", 18, 204);

  drawSmallLabel("Chamber:", 256, 154);
  drawSmallLabel("Fan:", 256, 180);
  drawSmallLabel("Aux:", 256, 204);

  // Dedicated AMS card, separated from the metric cards above.
  drawCard(6, 234, 468, 46);
  drawSmallLabel("AMS:", 18, 248);

  drawBottomButtons();
}
void updateHUDValues() {
  if (SCREEN_W <= 320) {
    updateHUDValuesCompact();
    return;
  }

  useValueFont();

  if (millis() - lastClockRead > 1000 || lastClockRead == 0) {
    lastClockRead = millis();
    updateClockText();
  }

  if (hudClockText != lastHudClockText) {
    drawClockIndicator();
    lastHudClockText = hudClockText;

    // Clock changes can also change the ETA display.
    etaText = getETAText();
    if (etaText != lastEtaText) {
      tft.fillRect(370, 86, 92, 20, themePanel());
      tft.setTextColor(themeGreen(), themePanel());
      tft.drawString(etaText, 370, 86);
      lastEtaText = etaText;
    }
  }

  if (printerState != lastPrinterState) {
    tft.fillRect(110, 55, 175, 22, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    String stateText = printerState;
    if (stateText.length() > 10) stateText = stateText.substring(0, 10);
    tft.drawString(stateText, 110, 55);
    lastPrinterState = printerState;
  }

  if (millis() - lastBatteryRead > 5000 || lastBatteryRead == 0) {
    lastBatteryRead = millis();
    updateBatteryStatus();
  }

  if (batteryPercent != lastBatteryPercent ||
      batteryChargingEstimate != lastBatteryChargingEstimate ||
      abs(batteryVoltage - lastBatteryVoltage) >= 0.04) {
    drawBatteryIndicator();
    lastBatteryPercent = batteryPercent;
    lastBatteryVoltage = batteryVoltage;
    lastBatteryChargingEstimate = batteryChargingEstimate;
  }

  if (progress != lastProgress) {
    tft.fillRect(125, 85, 80, 22, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawString(String(progress) + "%", 125, 85);

    tft.fillRoundRect(20, 117, 440, 14, 7, themePanelDark());

    int barWidth = map(progress, 0, 100, 0, 440);
    tft.fillRoundRect(20, 117, barWidth, 14, 7, themeGreen());

    lastProgress = progress;
  }

  if (nozzleTemp != lastNozzleTemp) {
    tft.fillRect(126, 154, 95, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawString(String(nozzleTemp, 1) + " C", 126, 154);
    lastNozzleTemp = nozzleTemp;
  }

  if (bedTemp != lastBedTemp) {
    tft.fillRect(126, 180, 95, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawString(String(bedTemp, 1) + " C", 126, 180);
    lastBedTemp = bedTemp;
  }

  if (chamberTemp != lastChamberTemp) {
    tft.fillRect(380, 154, 85, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    if (chamberTemp > 0) tft.drawString(String(chamberTemp, 1) + " C", 380, 154);
    else tft.drawString("--", 380, 154);
    lastChamberTemp = chamberTemp;
  }

  if (partFanSpeed != lastPartFanSpeed) {
    tft.fillRect(330, 180, 75, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    if (partFanSpeed >= 0) tft.drawString(String(partFanSpeed) + "%", 330, 180);
    else tft.drawString("--", 330, 180);
    lastPartFanSpeed = partFanSpeed;
  }

  if (auxFanSpeed != lastAuxFanSpeed) {
    tft.fillRect(330, 204, 75, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    if (auxFanSpeed >= 0) tft.drawString(String(auxFanSpeed) + "%", 330, 204);
    else tft.drawString("--", 330, 204);
    lastAuxFanSpeed = auxFanSpeed;
  }

  if (amsSummary != lastAmsSummary) {
    drawAMSLine();
    lastAmsSummary = amsSummary;
  }

  if (layerNum != lastLayerNum) {
    tft.fillRect(126, 204, 90, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    if (layerNum > 0) tft.drawString(String(layerNum), 126, 204);
    else tft.drawString("0", 126, 204);
    lastLayerNum = layerNum;
  }

  if (remainingMinutes != lastRemainingMinutes) {
    int h = remainingMinutes / 60;
    int m = remainingMinutes % 60;

    String remainingText = String(h) + "h " + String(m) + "m";
    if (remainingMinutes <= 0) {
      remainingText = "--";
    }

    tft.fillRect(370, 56, 90, 20, themePanel());
    useValueFont();
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawString(remainingText, 370, 56);

    etaText = getETAText();
    tft.fillRect(370, 86, 92, 20, themePanel());
    useValueFont();
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawString(etaText, 370, 86);
    lastEtaText = etaText;

    lastRemainingMinutes = remainingMinutes;
  }

  // Filename display removed because Bambu MQTT does not send filenames consistently.
  lastDisplayedFileName = displayedFileName;
}

void drawHUD() {
  if (!hudDrawn) {
    drawHUDLayout();
    hudDrawn = true;

    lastPrinterState = "";
    lastWifiSignal = "";
    lastProgress = -1;
    lastNozzleTemp = -999;
    lastBedTemp = -999;
    lastLayerNum = -1;
    lastRemainingMinutes = -1;
    lastDisplayedFileName = "__FORCE__";
    lastAmsSummary = "__FORCE__";
    lastBatteryPercent = -999;
    lastBatteryVoltage = -999;
    lastBatteryChargingEstimate = false;
    lastHudClockText = "__FORCE__";
    lastEtaText = "__FORCE__";
    updateClockText();

    // The compact 2.8" CYD HUD draws its own small clock in drawHUDLayoutCompact().
    // Do not call the original 4" header clock here, or it creates a second large clock.
    if (SCREEN_W > 320) {
      drawClockIndicator();
      updateBatteryStatus();
      drawBatteryIndicator();
    }

    // Draw saved/manual AMS colors immediately on HUD load,
    // even before AMS data is received from the printer.
    if (SCREEN_W <= 320) drawAMSLineCompact(177);
    else drawAMSLine();
  }

  updateHUDValues();
}

void drawNetworkPage() {
  if (SCREEN_W <= 320) {
    drawGradientBackground();
    drawCard(4, 4, SCREEN_W - 8, 28);
    useClassicFont(1);
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawString("Printers", 12, 13);

    drawCard(4, 40, SCREEN_W - 8, 164);
    useClassicFont(1);
    if (printerCount == 0) {
      tft.setTextColor(TFT_RED, themePanel());
      tft.drawString("No printers saved", 14, 58);
      tft.setTextColor(TFT_WHITE, themePanel());
      tft.drawString("Open web portal", 14, 82);
      tft.drawString(WiFi.localIP().toString(), 14, 106);
    }
    else {
      for (int i = 0; i < printerCount && i < 4; i++) {
        int y = 58 + i * 32;
        if (i == selectedPrinter) {
          tft.fillRoundRect(10, y - 5, 300, 25, 4, themePanelDark());
          tft.setTextColor(themeGreen(), themePanelDark());
          drawFittedString("> " + printers[i].name, 14, y, 135, themeGreen(), themePanelDark());
          drawRightFittedString(printers[i].ip, 304, y, 130, TFT_YELLOW, themePanelDark());
        } else {
          tft.setTextColor(TFT_WHITE, themePanel());
          drawFittedString("  " + printers[i].name, 14, y, 135, TFT_WHITE, themePanel());
          drawRightFittedString(printers[i].ip, 304, y, 130, TFT_YELLOW, themePanel());
        }
      }
      tft.setTextColor(themeTextMuted(), themePanel());
      tft.drawString("Tap printer to connect", 14, 184);
    }
    drawBottomButtons();
    return;
  }

  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  tft.drawString("Printers", 10, 10);
  tft.drawLine(0, 40, 480, 40, TFT_DARKGREY);

  if (printerCount == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No printers saved", 10, 75);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Open web portal", 10, 115);
    tft.drawString(WiFi.localIP().toString(), 10, 150);
  }
  else {
    for (int i = 0; i < printerCount && i < 5; i++) {
      int y = 70 + (i * 35);

      if (i == selectedPrinter) {
        tft.fillRect(5, y - 3, 470, 29, TFT_DARKGREY);
        tft.setTextColor(TFT_GREEN, TFT_DARKGREY);
        tft.drawString("> " + printers[i].name, 10, y);
      }
      else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("  " + printers[i].name, 10, y);
      }

      tft.setTextColor(TFT_YELLOW, i == selectedPrinter ? TFT_DARKGREY : TFT_BLACK);
      tft.drawString(printers[i].ip, 250, y);
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Tap printer to connect", 10, 250);
  }

  drawBottomButtons();
}

void drawSettingsPage() {
  if (SCREEN_W <= 320) {
    drawGradientBackground();
    drawCard(4, 4, SCREEN_W - 8, 28);
    useClassicFont(1);
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawString("Settings", 12, 13);

    drawCard(4, 40, SCREEN_W - 8, 164);
    useClassicFont(1);
    tft.setTextColor(TFT_WHITE, themePanel());

    tft.drawString("AMS Colors", 14, 55);
    tft.drawRoundRect(10, 49, 122, 26, 5, themeLine());

    tft.drawString("Status", 197, 55);
    tft.drawRoundRect(190, 49, 120, 26, 5, themeLine());

    String rotationText = "Rotate: " + String(screenRotation == 1 ? "Normal" : "180");
    drawFittedString(rotationText, 14, 86, 135, TFT_WHITE, themePanel());
    tft.drawRoundRect(10, 80, 145, 26, 5, themeLine());

    String themeText = "Theme: " + getThemeName();
    drawFittedString(themeText, 14, 117, 180, TFT_WHITE, themePanel());
    tft.drawRoundRect(10, 111, 205, 26, 5, themeLine());

    String psText = "Power: " + getPowerSaveModeLabel();
    drawFittedString(psText, 14, 148, 130, TFT_WHITE, themePanel());
    tft.drawRoundRect(10, 142, 145, 26, 5, themeLine());

    String timeoutText = "Timeout: " + getPowerSaveTimeoutLabel();
    drawFittedString(timeoutText, 168, 148, 130, TFT_WHITE, themePanel());
    tft.drawRoundRect(164, 142, 146, 26, 5, themeLine());

    String styleText = "Font: " + getDisplayStyleName();
    drawFittedString(styleText, 14, 179, 135, TFT_WHITE, themePanel());
    tft.drawRoundRect(10, 173, 145, 26, 5, themeLine());

    String rssi = wifiSignal.length() ? wifiSignal : String(WiFi.RSSI()) + "dBm";
    drawRightFittedString(rssi, 306, 179, 70, TFT_WHITE, themePanel());
    drawFittedString(WiFi.localIP().toString(), 176, 179, 82, themeTextMuted(), themePanel());

    drawBottomButtons();
    return;
  }

  drawGradientBackground();
  drawEmbossedFeathers();

  drawCard(6, 6, 468, 32);
  tft.setTextColor(themeGreen(), themePanel());
  useHeaderFont();
  tft.drawString("Settings", 18, 14);

  drawCard(6, 55, 468, 220);

  tft.setTextColor(TFT_WHITE, themePanel());
  useValueFont();

  tft.drawString("AMS Colors", 18, 78);
  tft.drawRoundRect(12, 70, 220, 32, 6, themeLine());

  tft.drawString("Status", 315, 78);
  tft.drawRoundRect(300, 70, 165, 32, 6, themeLine());

  String rotationText = "Rotate: ";
  rotationText += (screenRotation == 1) ? "Normal" : "180 deg";
  tft.drawString(rotationText, 18, 118);
  tft.drawRoundRect(12, 110, 270, 32, 6, themeLine());

  String themeText = "Theme: " + getThemeName();
  if (themeText.length() > 24) themeText = themeText.substring(0, 24);
  tft.drawString(themeText, 18, 158);
  tft.drawRoundRect(12, 150, 330, 32, 6, themeLine());

  // Small theme preview swatches.
  tft.fillRoundRect(355, 154, 28, 22, 4, themePanelDark());
  tft.drawRoundRect(355, 154, 28, 22, 4, themeLine());
  tft.fillRoundRect(388, 154, 28, 22, 4, themePanel());
  tft.drawRoundRect(388, 154, 28, 22, 4, themeLine());
  tft.fillRoundRect(421, 154, 28, 22, 4, themeGreen());
  tft.drawRoundRect(421, 154, 28, 22, 4, TFT_WHITE);

  // Power management controls.
  tft.setTextColor(TFT_WHITE, themePanel());
  String psText = "Power: " + getPowerSaveModeLabel();
  if (psText.length() > 17) psText = psText.substring(0, 17);
  tft.drawString(psText, 18, 198);
  tft.drawRoundRect(12, 190, 220, 32, 6, themeLine());

  String timeoutText = "Timeout: " + getPowerSaveTimeoutLabel();
  if (timeoutText.length() > 17) timeoutText = timeoutText.substring(0, 17);
  tft.drawString(timeoutText, 252, 198);
  tft.drawRoundRect(246, 190, 220, 32, 6, themeLine());

  // Font selector. Tap to cycle Classic / Modern Sans / Bold Sans / Mono / Serif.
  tft.setTextColor(TFT_WHITE, themePanel());
  useValueFont();
  String styleText = "Font: " + getDisplayStyleName();
  if (styleText.length() > 21) styleText = styleText.substring(0, 21);
  tft.drawString(styleText, 18, 238);
  tft.drawRoundRect(12, 230, 270, 32, 6, themeLine());

  String settingsWifi = wifiSignal;
  if (settingsWifi.length() == 0) {
    settingsWifi = String(WiFi.RSSI()) + "dBm";
  }

  // Keep right-side Settings info readable and inside the panel.
  tft.drawString("WiFi:", 300, 232);
  String wifiShort = settingsWifi;
  if (wifiShort.length() > 8) wifiShort = wifiShort.substring(0, 8);
  tft.drawString(wifiShort, 365, 232);

  tft.drawString("IP:", 300, 255);
  tft.setTextColor(TFT_WHITE, themePanel());
  String hudIP = WiFi.localIP().toString();
  tft.drawString(hudIP, 335, 255);

  drawBottomButtons();
}

void drawStatsPage() {
  if (SCREEN_W <= 320) {
    drawGradientBackground();
    drawCard(4, 4, SCREEN_W - 8, 28);
    useClassicFont(1);
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawString("System Stats", 12, 13);

    drawCard(4, 40, SCREEN_W - 8, 164);
    String mqttText = mqtt.connected() ? "Connected" : "Disc";
    String ipText = WiFi.localIP().toString();
    String rssiText = String(WiFi.RSSI()) + "dBm";
    String heapText = String(ESP.getFreeHeap() / 1024) + "KB";
    String uptimeText = getUptimeText();
    String printerText = printerName;

    int y = 52;
    int rowH = 18;
    auto drawStatRowCompact = [&](const String& label, const String& value) {
      useClassicFont(1);
      tft.setTextColor(themeTextMuted(), themePanel());
      tft.drawString(label, 14, y);
      drawRightFittedString(value, 306, y, 185, TFT_WHITE, themePanel());
      y += rowH;
    };

    drawStatRowCompact("Printer:", printerText);
    drawStatRowCompact("MQTT:", mqttText);
    drawStatRowCompact("WiFi:", rssiText);
    drawStatRowCompact("IP:", ipText);
    drawStatRowCompact("Heap:", heapText);
    drawStatRowCompact("Uptime:", uptimeText);
    drawStatRowCompact("Theme:", getThemeName());
    drawStatRowCompact("Font:", getDisplayStyleName());

    drawBottomButtons();
    return;
  }

  drawGradientBackground();
  drawEmbossedFeathers();

  drawCard(6, 6, 468, 32);
  tft.setTextColor(themeGreen(), themePanel());
  tft.setTextSize(2);
  tft.drawString("System Stats", 18, 14);

  drawCard(6, 50, 468, 228);

  String mqttText = mqtt.connected() ? "Connected" : "Disc";
  String ipText = WiFi.localIP().toString();
  String rssiText = String(WiFi.RSSI()) + "dBm";
  String battText = String(batteryVoltage, 2) + "V  " + String(batteryPercent) + "%";
  String powerText = batteryChargingEstimate ? "USB/Charge" : "Battery";
  String heapText = String(ESP.getFreeHeap() / 1024) + "KB";
  String uptimeText = getUptimeText();
  String themeText = getThemeName();
  String printerText = printerName;

  if (printerText.length() > 21) printerText = printerText.substring(0, 21);
  if (themeText.length() > 20) themeText = themeText.substring(0, 20);

  // Single-column layout prevents right-edge clipping on the 480px display.
  int labelX = 18;
  int valueX = 135;
  int y = 56;
  int rowH = 19;

  tft.setTextSize(2);

  auto drawStatRow = [&](const String& label, const String& value) {
    tft.setTextColor(themeTextMuted(), themePanel());
    tft.drawString(label, labelX, y);
    tft.setTextColor(TFT_WHITE, themePanel());
    tft.drawString(value, valueX, y);
    y += rowH;
  };

  drawStatRow("Printer:", printerText);
  drawStatRow("MQTT:", mqttText);
  drawStatRow("WiFi:", rssiText);
  drawStatRow("IP:", ipText);
  drawStatRow("Batt:", battText);
  drawStatRow("Power:", powerText);
  drawStatRow("Heap:", heapText);
  drawStatRow("Uptime:", uptimeText);
  drawStatRow("Theme:", themeText);
  drawStatRow("Font:", getDisplayStyleName());
  drawStatRow("Save:", getPowerSaveModeLabel() + " " + getPowerSaveTimeoutLabel());
  drawStatRow("ETA:", getETAText());

  drawBottomButtons();
}

void drawAMSColorsPage() {
  if (SCREEN_W <= 320) {
    drawGradientBackground();
    drawCard(4, 4, SCREEN_W - 8, 28);
    useClassicFont(1);
    tft.setTextColor(themeGreen(), themePanel());
    tft.drawString("AMS Color Setup", 12, 13);

    tft.setTextColor(themeTextMuted(), TFT_BLACK);
    tft.drawString("Tap slot, then color", 10, 38);

    for (int i = 0; i < 4; i++) {
      int x = 14 + i * 76;
      int y = 58;
      uint16_t c = amsColorToTFT(getAMSDisplayColor(i));
      if (i == selectedAmsColorSlot) {
        tft.drawRect(x - 3, y - 3, 60, 40, TFT_YELLOW);
      } else {
        tft.drawRect(x - 3, y - 3, 60, 40, TFT_DARKGREY);
      }
      tft.fillRoundRect(x, y, 54, 34, 5, c);
      tft.drawRoundRect(x, y, 54, 34, 5, TFT_WHITE);
      tft.setTextColor(TFT_WHITE, c);
      tft.drawCentreString(String(i + 1), x + 27, y + 12, 1);
    }

    String colors[12] = {
      "#000000", "#FFFFFF", "#FF0000", "#0000FF",
      "#00FF00", "#FFFF00", "#FF8000", "#8000FF",
      "#808080", "#FF66CC", "#8B4513", "#222222"
    };
    for (int i = 0; i < 12; i++) {
      int col = i % 4;
      int row = i / 4;
      int x = 16 + col * 76;
      int y = 112 + row * 30;
      uint16_t c = amsColorToTFT(colors[i]);
      tft.fillRoundRect(x, y, 58, 22, 5, c);
      tft.drawRoundRect(x, y, 58, 22, 5, TFT_WHITE);
    }

    drawBottomButtons();
    return;
  }

  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  tft.drawString("AMS Color Setup", 10, 10);
  tft.drawLine(0, 40, 480, 40, TFT_DARKGREY);

  tft.setTextSize(1);
  tft.drawString("Tap a slot, then tap a color", 10, 48);

  for (int i = 0; i < 4; i++) {
    int x = 15 + (i * 115);
    int y = 70;

    uint16_t c = amsColorToTFT(getAMSDisplayColor(i));

    if (i == selectedAmsColorSlot) {
      tft.drawRect(x - 4, y - 4, 82, 52, TFT_YELLOW);
      tft.drawRect(x - 3, y - 3, 80, 50, TFT_YELLOW);
    }
    else {
      tft.drawRect(x - 4, y - 4, 82, 52, TFT_DARKGREY);
    }

    tft.fillRect(x, y, 74, 26, c);
    tft.drawRect(x, y, 74, 26, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Slot " + String(i + 1), x + 8, y + 32);
  }

  struct ColorButton {
    const char* name;
    const char* hex;
  };

  ColorButton colors[12] = {
    {"Black", "#000000"},
    {"White", "#FFFFFF"},
    {"Red", "#FF0000"},
    {"Blue", "#0000FF"},
    {"Green", "#00FF00"},
    {"Yellow", "#FFFF00"},
    {"Orange", "#FF8000"},
    {"Purple", "#8000FF"},
    {"Gray", "#808080"},
    {"Pink", "#FF66CC"},
    {"Brown", "#8B4513"},
    {"Empty", "#222222"}
  };

  for (int i = 0; i < 12; i++) {
    int col = i % 4;
    int row = i / 4;

    int x = 15 + (col * 115);
    int y = 135 + (row * 45);

    tft.fillRect(x, y, 95, 30, amsColorToTFT(String(colors[i].hex)));
    tft.drawRect(x, y, 95, 30, TFT_WHITE);

    // Use black text on light colors.
    String colorName = String(colors[i].name);
    if (colorName == "White" || colorName == "Yellow") {
      tft.setTextColor(TFT_BLACK);
    }
    else {
      tft.setTextColor(TFT_WHITE);
    }

    tft.drawString(colorName, x + 8, y + 10);
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Colors save automatically", 10, 270);

  drawBottomButtons();
}

// =====================
// OTA
// =====================

void setupOTA()
{
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/saveWifi", HTTP_POST, handleSaveWifi);
  server.on("/saveTime", HTTP_POST, handleSaveTime);
  server.on("/addPrinter", HTTP_POST, handleAddPrinter);
  server.on("/deletePrinter", HTTP_GET, handleDeletePrinter);
  server.on("/selectPrinter", HTTP_GET, handleSelectPrinter);
  server.on("/requestStatus", HTTP_GET, handleRequestStatus);
  server.on("/reboot", HTTP_GET, handleReboot);

  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html",
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<h2>Bambu HUD OTA Update</h2>"
      "<input type='file' name='update'>"
      "<br><br>"
      "<input type='submit' value='Update Firmware'>"
      "</form>"
    );
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "Update Failed" : "Update Complete. Rebooting...");
    delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      Update.begin(UPDATE_SIZE_UNKNOWN);
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    }
    else if (upload.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
  });

  server.begin();
}



// =====================
// Setup / Loop
// =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=================================");
  Serial.println("     BAMBU HUD STARTING");
  Serial.println("=================================");
  analogReadResolution(12);
#if HAS_BATTERY_MONITOR
  pinMode(BAT_ADC_PIN, INPUT);
#endif

  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  setBacklightLevel(255);
  lastUserActivity = millis();

  tft.init();

  SPI.begin(TOUCH_SPI_SCLK, TOUCH_SPI_MISO, TOUCH_SPI_MOSI, TOUCH_CS);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  ts.begin();

  loadSettings();
  applyScreenRotation();
  tft.fillScreen(TFT_BLACK);

  checkFactoryReset();

  if (!hasConfig) {
    startSetupPortal();
  }
  else {
    if (connectWiFi()) {
  configureClock();
  setupOTA();
  connectMQTT();
   }
  }
  Serial.println("=================================");
  Serial.println("      SETUP COMPLETE");
  Serial.println("=================================");

}

void loop() {
  if (!hasConfig) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;
  }

  static unsigned long lastMqttReconnect = 0;

  if (!mqtt.connected() && WiFi.status() == WL_CONNECTED) {
    if (millis() - lastMqttReconnect > 10000) {
      lastMqttReconnect = millis();

      Serial.println("MQTT disconnected. Reconnecting...");
      Serial.print("Previous MQTT state: ");
      Serial.println(mqtt.state());

      connectMQTT();

      Serial.print("New MQTT state: ");
      Serial.println(mqtt.state());
    }
  }

  if (mqtt.connected()) {
    mqtt.loop();
  }

  server.handleClient();

  handleTouch();
  updatePowerSave();
  updateCompletedPrintReset();

  if (currentPage == PAGE_DASHBOARD) {
    if (millis() - lastScreenUpdate > 250) {
      lastScreenUpdate = millis();
      drawHUD();
    }
  }

  if (currentPage == PAGE_NETWORK && !hudDrawn) {
    drawNetworkPage();
    hudDrawn = true;
  }

  if (currentPage == PAGE_SETTINGS && !hudDrawn) {
    drawSettingsPage();
    hudDrawn = true;
  }

  if (currentPage == PAGE_AMS_COLORS && !hudDrawn) {
    drawAMSColorsPage();
    hudDrawn = true;
  }

  if (currentPage == PAGE_STATS && !hudDrawn) {
    drawStatsPage();
    hudDrawn = true;
  }

  static unsigned long lastRequest = 0;

  if (millis() - lastRequest > 300000) {
    lastRequest = millis();
    requestFullStatus();
  }


  if (millis() - lastHeartbeat > 10000) {
    lastHeartbeat = millis();

    Serial.println("----- HUD ALIVE -----");
    Serial.print("Printer: ");
    Serial.println(printerName);

    Serial.print("State: ");
    Serial.println(printerState);

    Serial.print("MQTT: ");
    Serial.print(mqtt.connected() ? "Connected" : "Disconnected");
    Serial.print(" State: ");
    Serial.println(mqtt.state());

    Serial.print("WiFi RSSI: ");
    Serial.println(WiFi.RSSI());

#if HAS_BATTERY_MONITOR
    Serial.print("Battery: ");
    Serial.print(batteryVoltage, 2);
    Serial.print("V ");
    Serial.print(batteryPercent);
    Serial.println("%");
#else
    Serial.println("Battery: N/A on this screen profile");
#endif

    Serial.println("---------------------");
  }
}