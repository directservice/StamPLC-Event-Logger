/*
 * StamPLC Event Logger with LCD Display
 * 
 * Monitors 8 digital inputs and logs state changes to SD card.
 * Displays real-time status on the 1.14-inch LCD screen.
 * 
 * SPDX-License-Identifier: MIT
 */

#include <Arduino.h>
#include <M5StamPLC.h>
#include <SD.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

const uint8_t INPUT_COUNT = 8;
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000;  // Update display every 1 second
const unsigned long DEBOUNCE_MS = 50;                 // Debounce delay in milliseconds

// CSV file names for each input channel
const char* inputFileNames[INPUT_COUNT] = {
  "/IN0.csv",
  "/IN1.csv",
  "/IN2.csv",
  "/IN3.csv",
  "/IN4.csv",
  "/IN5.csv",
  "/IN6.csv",
  "/IN7.csv"
};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

bool previousState[INPUT_COUNT];
unsigned long lastChangeTime[INPUT_COUNT];
bool sdCardReady = false;
unsigned long lastDisplayUpdate = 0;
unsigned long totalEventsLogged = 0;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Get formatted timestamp from RTC
 * Format: YYYY-MM-DD HH:MM:SS
 */
String formatTimestamp()
{
  struct tm now;
  M5StamPLC.getRtcTime(&now);

  char buffer[25];
  snprintf(buffer, sizeof(buffer),
           "%04d-%02d-%02d %02d:%02d:%02d",
           now.tm_year + 1900,
           now.tm_mon + 1,
           now.tm_mday,
           now.tm_hour,
           now.tm_min,
           now.tm_sec);

  return String(buffer);
}

/**
 * Create CSV file header if file doesn't exist
 */
void createLogFileIfNeeded(uint8_t channel)
{
  String path = String(inputFileNames[channel]);

  if (!SD.exists(path.c_str())) {
    File file = SD.open(path.c_str(), FILE_WRITE);
    if (file) {
      file.println("Timestamp,State");
      file.close();
      Serial.printf("Created log file: %s\n", path.c_str());
    } else {
      Serial.printf("Failed to create log file: %s\n", path.c_str());
    }
  }
}

/**
 * Append state change event to CSV file
 */
void appendEvent(uint8_t channel, const String& state)
{
  if (!sdCardReady) {
    Serial.println("SD card not ready, cannot log event!");
    return;
  }

  String path = String(inputFileNames[channel]);

  File file = SD.open(path.c_str(), FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open file for writing: %s\n", path.c_str());
    return;
  }

  file.printf("%s,%s\n", formatTimestamp().c_str(), state.c_str());
  file.close();

  Serial.printf("[%s] %s -> %s\n", path.c_str(), formatTimestamp().c_str(), state.c_str());
  totalEventsLogged++;
}

/**
 * Count how many inputs are currently active
 */
uint8_t countActiveInputs()
{
  uint8_t activeCount = 0;
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    if (previousState[i]) {
      activeCount++;
    }
  }
  return activeCount;
}

/**
 * Get SD card status text
 */
String getSdStatus()
{
  return sdCardReady ? "OK" : "ERROR";
}

/**
 * Get SD card status color (RGB565)
 */
uint16_t getSdStatusColor()
{
  return sdCardReady ? 0x07E0 : 0xF800;  // Green : Red
}

/**
 * Update LCD display with current status
 */
void updateDisplay()
{
  unsigned long currentTime = millis();
  if ((currentTime - lastDisplayUpdate) < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  lastDisplayUpdate = currentTime;

  // Get current time
  struct tm now;
  M5StamPLC.getRtcTime(&now);

  // Clear display
  M5StamPLC.Lcd().fillScreen(0x0000);  // Black background

  // Set text properties
  M5StamPLC.Lcd().setTextSize(1);
  M5StamPLC.Lcd().setTextColor(0xFFFF, 0x0000);  // White text, black background

  int16_t x = 5;
  int16_t y = 5;
  const int16_t lineHeight = 14;

  // ========== HEADER ==========
  M5StamPLC.Lcd().setTextSize(2);
  M5StamPLC.Lcd().setTextColor(0x07E0);  // Green
  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().print("StamPLC Logger");
  y += lineHeight * 2;

  M5StamPLC.Lcd().setTextSize(1);
  M5StamPLC.Lcd().setTextColor(0xFFFF);  // White

  // ========== TIMESTAMP ==========
  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().printf("Time: %04d-%02d-%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  y += lineHeight;

  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().printf("      %02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
  y += lineHeight;

  // ========== SD CARD STATUS ==========
  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().setTextColor(0xFFFF);  // White
  M5StamPLC.Lcd().print("SD Card: ");
  M5StamPLC.Lcd().setTextColor(getSdStatusColor());
  M5StamPLC.Lcd().print(getSdStatus().c_str());
  y += lineHeight;

  // ========== ACTIVE INPUTS ==========
  uint8_t activeCount = countActiveInputs();
  M5StamPLC.Lcd().setTextColor(0xFFFF);
  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().printf("Active Inputs: %d/8", activeCount);
  y += lineHeight;

  // ========== TOTAL EVENTS ==========
  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().printf("Total Events: %lu", totalEventsLogged);
  y += lineHeight * 2;

  // ========== INPUT STATES ==========
  M5StamPLC.Lcd().setTextColor(0xFFE0);  // Yellow
  M5StamPLC.Lcd().setCursor(x, y);
  M5StamPLC.Lcd().print("Input States:");
  y += lineHeight;

  M5StamPLC.Lcd().setTextColor(0xFFFF);  // White
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    M5StamPLC.Lcd().setCursor(x + (i % 4) * 30, y + (i / 4) * lineHeight);
    M5StamPLC.Lcd().setTextColor(previousState[i] ? 0x07E0 : 0xF800);  // Green if ON, Red if OFF
    M5StamPLC.Lcd().printf("IN%d:%s ", i, previousState[i] ? "ON " : "OFF");
  }

  y += lineHeight * 2;

  // ========== BUTTON INSTRUCTIONS ==========
  M5StamPLC.Lcd().setTextColor(0x07FF);  // Cyan
  M5StamPLC.Lcd().setTextSize(1);
  M5StamPLC.Lcd().setCursor(x, 108);
  M5StamPLC.Lcd().print("A:Reset  B:Log  C:Info");
}

/**
 * Display detailed SD card info when button B is pressed
 */
void displayLogInfo()
{
  M5StamPLC.Lcd().fillScreen(0x0000);
  M5StamPLC.Lcd().setTextSize(1);
  M5StamPLC.Lcd().setTextColor(0xFFFF);
  M5StamPLC.Lcd().setCursor(5, 5);

  M5StamPLC.Lcd().println("=== Log File Info ===");
  M5StamPLC.Lcd().println("");

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    String path = String(inputFileNames[i]);
    if (SD.exists(path.c_str())) {
      File file = SD.open(path.c_str());
      if (file) {
        uint32_t fileSize = file.size();
        file.close();
        M5StamPLC.Lcd().printf("%s: %lu bytes\n", path.c_str(), fileSize);
      }
    } else {
      M5StamPLC.Lcd().printf("%s: Not found\n", path.c_str());
    }
  }

  M5StamPLC.Lcd().println("");
  M5StamPLC.Lcd().println("Press any key to return");
  M5StamPLC.tone(1500, 100);

  // Wait for button press
  while (!M5StamPLC.BtnA().wasPressed() && !M5StamPLC.BtnB().wasPressed() && !M5StamPLC.BtnC().wasPressed()) {
    M5StamPLC.update();
    delay(50);
  }

  // Consume button presses
  M5StamPLC.BtnA().wasPressed();
  M5StamPLC.BtnB().wasPressed();
  M5StamPLC.BtnC().wasPressed();
}

/**
 * Display detailed input info when button C is pressed
 */
void displayInputInfo()
{
  M5StamPLC.Lcd().fillScreen(0x0000);
  M5StamPLC.Lcd().setTextSize(1);
  M5StamPLC.Lcd().setTextColor(0xFFFF);
  M5StamPLC.Lcd().setCursor(5, 5);

  M5StamPLC.Lcd().println("=== Current Inputs ===");
  M5StamPLC.Lcd().println("");

  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    bool state = M5StamPLC.readPlcInput(i);
    M5StamPLC.Lcd().setTextColor(state ? 0x07E0 : 0xF800);
    M5StamPLC.Lcd().printf("IN%d: %s\n", i, state ? "ACTIVE  (1)" : "INACTIVE (0)");
  }

  M5StamPLC.Lcd().setTextColor(0xFFFF);
  M5StamPLC.Lcd().println("");
  M5StamPLC.Lcd().println("Press any key to return");
  M5StamPLC.tone(1500, 100);

  // Wait for button press
  while (!M5StamPLC.BtnA().wasPressed() && !M5StamPLC.BtnB().wasPressed() && !M5StamPLC.BtnC().wasPressed()) {
    M5StamPLC.update();
    delay(50);
  }

  // Consume button presses
  M5StamPLC.BtnA().wasPressed();
  M5StamPLC.BtnB().wasPressed();
  M5StamPLC.BtnC().wasPressed();
}

// ============================================================================
// SETUP
// ============================================================================

void setup()
{
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n\n=== StamPLC Event Logger Starting ===\n");

  // Configure SD card support
  auto config = M5StamPLC.config();
  config.enableSdCard = true;
  M5StamPLC.config(config);

  // Initialize StamPLC
  M5StamPLC.begin();

  // Initialize SD card
  sdCardReady = SD.begin();
  if (!sdCardReady) {
    Serial.println("WARNING: SD card initialization failed!");
  }

  // Initialize input states and create log files
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    previousState[i] = M5StamPLC.readPlcInput(i);
    lastChangeTime[i] = millis();
    if (sdCardReady) {
      createLogFileIfNeeded(i);
    }
  }

  // Set status light to green (ready)
  M5StamPLC.setStatusLight(0, 255, 0);

  Serial.println("Event logger initialized successfully!");
  Serial.printf("SD Card Status: %s\n", sdCardReady ? "Ready" : "Error");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop()
{
  M5StamPLC.update();

  // ========== INPUT MONITORING ==========
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    bool currentState = M5StamPLC.readPlcInput(i);
    unsigned long currentTime = millis();

    // Detect state change with debouncing
    if (currentState != previousState[i] && (currentTime - lastChangeTime[i]) >= DEBOUNCE_MS) {
      lastChangeTime[i] = currentTime;
      previousState[i] = currentState;

      String event = currentState ? "active" : "inactive";
      appendEvent(i, event);

      // Flash status light on state change
      if (currentState) {
        M5StamPLC.setStatusLight(0, 255, 0);  // Green for active
      } else {
        M5StamPLC.setStatusLight(128, 128, 128);  // Gray for inactive
      }
    }
  }

  // ========== DISPLAY UPDATE ==========
  updateDisplay();

  // ========== BUTTON HANDLING ==========
  if (M5StamPLC.BtnA().wasPressed()) {
    Serial.println("Button A pressed - Resetting event counter");
    totalEventsLogged = 0;
    M5StamPLC.tone(2000, 100);
  }

  if (M5StamPLC.BtnB().wasPressed()) {
    Serial.println("Button B pressed - Displaying log file info");
    displayLogInfo();
  }

  if (M5StamPLC.BtnC().wasPressed()) {
    Serial.println("Button C pressed - Displaying input details");
    displayInputInfo();
  }

  delay(10);
}

// ============================================================================
// END OF CODE
// ============================================================================
