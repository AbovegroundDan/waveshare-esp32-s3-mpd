#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>
#include "TCA9554.h"
#include "MPDAAFonts.h"
#include "TCCLogo46px.h"
#include "secrets.h"

#define GFX_BL 6

#define SPI_MISO 2
#define SPI_MOSI 1
#define SPI_SCLK 5

#define LCD_CS -1
#define LCD_DC 3
#define LCD_RST -1
#define LCD_HOR_RES 320
#define LCD_VER_RES 480

#define I2C_SDA 8
#define I2C_SCL 7

constexpr float WEATHER_LATITUDE = 40.7128;
constexpr float WEATHER_LONGITUDE = -74.0060;
constexpr char WEATHER_LOCATION[] = "NEW YORK, NY";

constexpr char WEATHER_URL[] =
  "https://api.open-meteo.com/v1/forecast"
  "?latitude=40.7128"
  "&longitude=-74.0060"
  "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
  "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
  "&temperature_unit=fahrenheit"
  "&wind_speed_unit=mph"
  "&timezone=America%2FNew_York"
  "&forecast_days=1";

constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long WEATHER_REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long WEATHER_RETRY_INTERVAL_MS = 60UL * 1000UL;

constexpr char NEW_YORK_TIME_ZONE[] = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.nist.gov";

constexpr uint16_t LCARS_BLACK = RGB565(0, 0, 0);
constexpr uint16_t LCARS_WHITE = RGB565(232, 236, 240);
constexpr uint16_t LCARS_ORANGE = RGB565(255, 153, 0);
constexpr uint16_t LCARS_GOLD = RGB565(255, 204, 102);
constexpr uint16_t LCARS_SALMON = RGB565(255, 153, 102);
constexpr uint16_t LCARS_LAVENDER = RGB565(204, 153, 204);
constexpr uint16_t LCARS_BLUE = RGB565(102, 153, 204);
constexpr uint16_t LCARS_RED = RGB565(204, 68, 68);

constexpr int16_t AA_TEXT_BUFFER_WIDTH = 480;
constexpr int16_t AA_TEXT_BUFFER_HEIGHT = 64;
constexpr int16_t HEADER_CLOCK_X = 202;
constexpr int16_t HEADER_CLOCK_Y = 4;
constexpr int16_t HEADER_CLOCK_WIDTH = 140;
constexpr int16_t HEADER_CLOCK_HEIGHT = 44;

TCA9554 TCA(0x20);

Arduino_DataBus* bus = new Arduino_ESP32SPI(
  LCD_DC,
  LCD_CS,
  SPI_SCLK,
  SPI_MOSI,
  SPI_MISO
);

Arduino_ST7796* gfx = new Arduino_ST7796(
  bus,
  LCD_RST,
  1,
  true,
  LCD_HOR_RES,
  LCD_VER_RES
);

unsigned long lastConnectionAttempt = 0;
unsigned long lastWeatherAttempt = 0;
unsigned long weatherAttemptInterval = WEATHER_RETRY_INTERVAL_MS;
bool clockConfigured = false;
unsigned long lastHeaderClockRefresh = 0;
char lastHeaderClock[12] = "";
uint16_t aaTextBuffer[AA_TEXT_BUFFER_WIDTH * AA_TEXT_BUFFER_HEIGHT];

struct AATextBounds {
  int16_t left;
  int16_t top;
  uint16_t width;
  uint16_t height;
};

void lcdReset() {
  TCA.write1(1, HIGH);
  delay(10);
  TCA.write1(1, LOW);
  delay(10);
  TCA.write1(1, HIGH);
  delay(200);
}

void stopWithError(const char* message) {
  Serial.println(message);

  while (true) {
    delay(1000);
  }
}

MPDAAGlyph readAAGlyph(const MPDAAFont& font, uint8_t character) {
  MPDAAGlyph glyph;
  memcpy_P(&glyph, font.glyphs + (character - font.first), sizeof(glyph));
  return glyph;
}

uint8_t supportedCharacter(const MPDAAFont& font, char character) {
  uint8_t code = static_cast<uint8_t>(character);
  if (code < font.first || code > font.last) {
    return '?';
  }
  return code;
}

AATextBounds measureAAText(const MPDAAFont& font, const char* text) {
  int16_t penX = 0;
  int16_t minX = INT16_MAX;
  int16_t minY = INT16_MAX;
  int16_t maxX = INT16_MIN;
  int16_t maxY = INT16_MIN;

  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    uint8_t character = supportedCharacter(font, *cursor);
    MPDAAGlyph glyph = readAAGlyph(font, character);

    if (glyph.width > 0 && glyph.height > 0) {
      int16_t glyphLeft = penX + glyph.xOffset;
      int16_t glyphTop = glyph.yOffset;
      minX = min(minX, glyphLeft);
      minY = min(minY, glyphTop);
      maxX = max(maxX, static_cast<int16_t>(glyphLeft + glyph.width));
      maxY = max(maxY, static_cast<int16_t>(glyphTop + glyph.height));
    }

    penX += glyph.xAdvance;
  }

  if (minX == INT16_MAX) {
    return {0, 0, 0, 0};
  }

  return {
    minX,
    minY,
    static_cast<uint16_t>(maxX - minX),
    static_cast<uint16_t>(maxY - minY)
  };
}

uint16_t blendRGB565(uint16_t foreground, uint16_t background, uint8_t alpha) {
  if (alpha == 255) return foreground;
  if (alpha == 0) return background;

  uint16_t inverseAlpha = 255 - alpha;
  uint16_t red = ((((foreground >> 11) & 0x1F) * alpha) +
                  (((background >> 11) & 0x1F) * inverseAlpha) + 127) / 255;
  uint16_t green = ((((foreground >> 5) & 0x3F) * alpha) +
                    (((background >> 5) & 0x3F) * inverseAlpha) + 127) / 255;
  uint16_t blue = (((foreground & 0x1F) * alpha) +
                   ((background & 0x1F) * inverseAlpha) + 127) / 255;
  return (red << 11) | (green << 5) | blue;
}

bool drawAAText(
  const MPDAAFont& font,
  const char* text,
  int16_t cursorX,
  int16_t baseline,
  uint16_t foreground,
  uint16_t background
) {
  AATextBounds bounds = measureAAText(font, text);
  if (bounds.width == 0 || bounds.height == 0) {
    return true;
  }

  if (bounds.width > AA_TEXT_BUFFER_WIDTH || bounds.height > AA_TEXT_BUFFER_HEIGHT) {
    Serial.printf(
      "ERROR: AA text buffer too small for %ux%u text: %s\n",
      bounds.width,
      bounds.height,
      text
    );
    return false;
  }

  uint32_t pixelCount = static_cast<uint32_t>(bounds.width) * bounds.height;
  for (uint32_t index = 0; index < pixelCount; ++index) {
    aaTextBuffer[index] = background;
  }

  int16_t penX = 0;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    uint8_t character = supportedCharacter(font, *cursor);
    MPDAAGlyph glyph = readAAGlyph(font, character);
    int16_t destinationX = penX + glyph.xOffset - bounds.left;
    int16_t destinationY = glyph.yOffset - bounds.top;

    for (uint8_t glyphY = 0; glyphY < glyph.height; ++glyphY) {
      for (uint8_t glyphX = 0; glyphX < glyph.width; ++glyphX) {
        uint32_t alphaOffset = glyph.bitmapOffset +
          (static_cast<uint32_t>(glyphY) * glyph.width) + glyphX;
        uint8_t alpha = pgm_read_byte(font.bitmap + alphaOffset);
        if (alpha == 0) continue;

        uint32_t bufferIndex =
          (static_cast<uint32_t>(destinationY + glyphY) * bounds.width) +
          destinationX + glyphX;
        aaTextBuffer[bufferIndex] = blendRGB565(
          foreground,
          aaTextBuffer[bufferIndex],
          alpha
        );
      }
    }

    penX += glyph.xAdvance;
  }

  gfx->draw16bitRGBBitmap(
    cursorX + bounds.left,
    baseline + bounds.top,
    aaTextBuffer,
    bounds.width,
    bounds.height
  );
  return true;
}

void drawAARightAligned(
  const MPDAAFont& font,
  const char* text,
  int16_t rightEdge,
  int16_t baseline,
  uint16_t foreground,
  uint16_t background
) {
  AATextBounds bounds = measureAAText(font, text);
  int16_t cursorX = rightEdge - bounds.width - bounds.left;
  drawAAText(font, text, cursorX, baseline, foreground, background);
}

void drawAACenteredInRect(
  const MPDAAFont& font,
  const char* text,
  int16_t x,
  int16_t y,
  int16_t width,
  int16_t height,
  uint16_t foreground,
  uint16_t background
) {
  AATextBounds bounds = measureAAText(font, text);
  int16_t cursorX = x + ((width - bounds.width) / 2) - bounds.left;
  int16_t baseline = y + ((height - bounds.height) / 2) - bounds.top;
  drawAAText(font, text, cursorX, baseline, foreground, background);
}

void getHeaderClockText(char* destination, size_t destinationSize) {
  struct tm timeInfo;
  if (!clockConfigured || !getLocalTime(&timeInfo, 0)) {
    snprintf(destination, destinationSize, "--:--");
    return;
  }

  strftime(destination, destinationSize, "%I:%M %p", &timeInfo);
  if (destination[0] == '0') {
    memmove(destination, destination + 1, strlen(destination));
  }
}

void updateHeaderClock(bool force = false) {
  if (!force && millis() - lastHeaderClockRefresh < 1000) {
    return;
  }

  lastHeaderClockRefresh = millis();
  char clockText[12];
  getHeaderClockText(clockText, sizeof(clockText));

  if (!force && strcmp(clockText, lastHeaderClock) == 0) {
    return;
  }

  gfx->fillRect(
    HEADER_CLOCK_X,
    HEADER_CLOCK_Y,
    HEADER_CLOCK_WIDTH,
    HEADER_CLOCK_HEIGHT,
    LCARS_ORANGE
  );
  drawAACenteredInRect(
    StarGuardItalic36AA,
    clockText,
    HEADER_CLOCK_X,
    HEADER_CLOCK_Y,
    HEADER_CLOCK_WIDTH,
    HEADER_CLOCK_HEIGHT,
    LCARS_BLACK,
    LCARS_ORANGE
  );
  snprintf(lastHeaderClock, sizeof(lastHeaderClock), "%s", clockText);
}

void drawMetricPill(
  int16_t x,
  int16_t y,
  int16_t width,
  int16_t height,
  uint16_t color,
  const char* text
) {
  gfx->fillRoundRect(x, y, width, height, 12, color);
  drawAACenteredInRect(
    StarGuardRegular26AA,
    text,
    x,
    y,
    width,
    height,
    LCARS_BLACK,
    color
  );
}

void drawHeader() {
  gfx->fillScreen(LCARS_BLACK);

  // LCARS-inspired frame and segmented left rail.
  gfx->fillRoundRect(0, 0, gfx->width(), 52, 18, LCARS_ORANGE);
  gfx->fillRoundRect(0, 60, 54, 70, 18, LCARS_SALMON);
  gfx->fillRoundRect(0, 138, 54, 50, 18, LCARS_GOLD);
  gfx->fillRoundRect(0, 196, 54, 52, 18, LCARS_LAVENDER);
  gfx->fillRoundRect(0, 256, 54, 64, 18, LCARS_BLUE);

  static_cast<Arduino_GFX*>(gfx)->draw16bitRGBBitmapWithMask(
    12, 3, TCCLogo46pxPixels, TCCLogo46pxMask, TCC_LOGO_WIDTH, TCC_LOGO_HEIGHT);

  drawAAText(
    StarGuardRegular26AA,
    "MPD WEATHER",
    70,
    35,
    LCARS_BLACK,
    LCARS_ORANGE
  );
  drawAARightAligned(
    StarGuardRegular26AA,
    WEATHER_LOCATION,
    458,
    35,
    LCARS_BLACK,
    LCARS_ORANGE
  );
  updateHeaderClock(true);
}

void showStatus(const char* title, const char* detail, uint16_t color) {
  drawHeader();

  gfx->fillRoundRect(74, 92, 390, 58, 20, color);
  drawAACenteredInRect(
    StarGuardItalic36AA,
    title,
    74,
    92,
    390,
    58,
    LCARS_BLACK,
    color
  );

  drawAAText(StarGuardRegular20AA, detail, 80, 186, LCARS_WHITE, LCARS_BLACK);

  gfx->fillRoundRect(80, 224, 100, 18, 9, LCARS_LAVENDER);
  gfx->fillRoundRect(188, 224, 170, 18, 9, LCARS_BLUE);
  gfx->fillRoundRect(366, 224, 96, 18, 9, LCARS_SALMON);
}

void configureLocalClock() {
  if (clockConfigured) {
    return;
  }

  configTzTime(NEW_YORK_TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);
  clockConfigured = true;

  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 10000)) {
    Serial.println("New York clock synchronized");
  } else {
    Serial.println("WARNING: NTP time is not available yet");
  }
}

String getLastCheckedTime() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 3000)) {
    return "--:--";
  }

  char timeText[16];
  strftime(timeText, sizeof(timeText), "%I:%M %p", &timeInfo);

  if (timeText[0] == '0') {
    return String(timeText + 1);
  }

  return String(timeText);
}

bool credentialsAreConfigured() {
  return String(WIFI_SSID) != "YOUR_WIFI_NAME" &&
         String(WIFI_PASSWORD) != "YOUR_WIFI_PASSWORD";
}

bool connectToWiFi() {
  lastConnectionAttempt = millis();

  if (!credentialsAreConfigured()) {
    showStatus("SET CREDENTIALS", "EDIT SECRETS.H, THEN UPLOAD AGAIN.", LCARS_RED);
    Serial.println("ERROR: Replace the placeholders in secrets.h");
    return false;
  }

  showStatus("CONNECTING", "WAITING FOR WI-FI...", LCARS_ORANGE);
  Serial.printf("Connecting to Wi-Fi network: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long connectionTimeout = 20000;
  unsigned long startedAt = millis();
  int progressStep = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < connectionTimeout) {
    gfx->fillCircle(82 + (progressStep * 22), 274, 4, LCARS_BLUE);
    progressStep = (progressStep + 1) % 16;
    delay(500);
    Serial.print('.');
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("NOT CONNECTED", "CHECK SECRETS.H. RETRYING IN 10 SEC.", LCARS_RED);
    Serial.println("Wi-Fi connection timed out");
    return false;
  }

  configureLocalClock();

  String ipAddress = WiFi.localIP().toString();
  showStatus("WI-FI CONNECTED", "REQUESTING NEW YORK WEATHER...", LCARS_BLUE);
  Serial.printf("Connected. IP address: %s\n", ipAddress.c_str());
  return true;
}

const char* weatherDescription(int weatherCode) {
  if (weatherCode == 0) return "Clear";
  if (weatherCode == 1) return "Mainly clear";
  if (weatherCode == 2) return "Partly cloudy";
  if (weatherCode == 3) return "Overcast";
  if (weatherCode == 45 || weatherCode == 48) return "Fog";
  if (weatherCode >= 51 && weatherCode <= 55) return "Drizzle";
  if (weatherCode == 56 || weatherCode == 57) return "Freezing drizzle";
  if (weatherCode >= 61 && weatherCode <= 65) return "Rain";
  if (weatherCode == 66 || weatherCode == 67) return "Freezing rain";
  if (weatherCode >= 71 && weatherCode <= 75) return "Snow";
  if (weatherCode == 77) return "Snow grains";
  if (weatherCode >= 80 && weatherCode <= 82) return "Rain showers";
  if (weatherCode == 85 || weatherCode == 86) return "Snow showers";
  if (weatherCode == 95) return "Thunderstorm";
  if (weatherCode == 96 || weatherCode == 99) return "T-storm / hail";
  return "Unknown";
}

void drawWeatherScreen(
  float temperature,
  float apparentTemperature,
  int humidity,
  int weatherCode,
  float windSpeed,
  float highTemperature,
  float lowTemperature,
  int rainChance,
  const String& lastCheckedTime
) {
  drawHeader();

  gfx->drawRoundRect(70, 66, 178, 104, 18, LCARS_BLUE);
  drawAAText(
    StarGuardRegular20AA,
    "TEMPERATURE",
    84,
    88,
    LCARS_BLUE,
    LCARS_BLACK
  );

  char temperatureText[16];
  snprintf(temperatureText, sizeof(temperatureText), "%.0f F", temperature);
  drawAAText(
    StarGuardItalic54AA,
    temperatureText,
    82,
    156,
    LCARS_GOLD,
    LCARS_BLACK
  );

  drawAAText(
    StarGuardRegular20AA,
    "CURRENT CONDITIONS",
    270,
    88,
    LCARS_LAVENDER,
    LCARS_BLACK
  );

  String condition = weatherDescription(weatherCode);
  condition.toUpperCase();
  drawAACenteredInRect(
    StarGuardItalic36AA,
    condition.c_str(),
    260,
    92,
    204,
    58,
    LCARS_WHITE,
    LCARS_BLACK
  );

  gfx->fillRoundRect(266, 151, 84, 17, 8, LCARS_SALMON);
  gfx->fillRoundRect(357, 151, 107, 17, 8, LCARS_LAVENDER);

  char metricText[24];

  snprintf(metricText, sizeof(metricText), "FEELS %.0f F", apparentTemperature);
  drawMetricPill(68, 184, 128, 40, LCARS_SALMON, metricText);

  snprintf(metricText, sizeof(metricText), "HIGH %.0f F", highTemperature);
  drawMetricPill(204, 184, 128, 40, LCARS_ORANGE, metricText);

  snprintf(metricText, sizeof(metricText), "LOW %.0f F", lowTemperature);
  drawMetricPill(340, 184, 128, 40, LCARS_LAVENDER, metricText);

  snprintf(metricText, sizeof(metricText), "HUMID %d%%", humidity);
  drawMetricPill(68, 232, 128, 40, LCARS_BLUE, metricText);

  snprintf(metricText, sizeof(metricText), "WIND %.0f MPH", windSpeed);
  drawMetricPill(204, 232, 128, 40, LCARS_GOLD, metricText);

  snprintf(metricText, sizeof(metricText), "RAIN %d%%", rainChance);
  drawMetricPill(340, 232, 128, 40, LCARS_SALMON, metricText);

  char lastCheckedText[40];
  snprintf(
    lastCheckedText,
    sizeof(lastCheckedText),
    "LAST CHECKED AT %s",
    lastCheckedTime.c_str()
  );
  drawAAText(
    StarGuardRegular20AA,
    lastCheckedText,
    70,
    307,
    LCARS_WHITE,
    LCARS_BLACK
  );
}

bool fetchAndDisplayWeather() {
  lastWeatherAttempt = millis();
  showStatus("GETTING WEATHER", "REQUESTING OPEN-METEO JSON...", LCARS_LAVENDER);
  Serial.println("Requesting Open-Meteo weather JSON");

  WiFiClientSecure secureClient;

  // Open-Meteo returns public data and uses no API key. Certificate validation
  // is disabled for this first beginner build to avoid embedding a CA certificate.
  secureClient.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(secureClient, WEATHER_URL)) {
    showStatus("WEATHER ERROR", "COULD NOT START HTTPS REQUEST.", LCARS_RED);
    Serial.println("ERROR: HTTPClient begin failed");
    return false;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    char errorMessage[48];
    snprintf(errorMessage, sizeof(errorMessage), "Open-Meteo HTTP status: %d", httpCode);
    showStatus("WEATHER ERROR", errorMessage, LCARS_RED);
    Serial.println(errorMessage);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  Serial.println("Open-Meteo JSON response:");
  Serial.println(payload);

  JsonDocument document;
  DeserializationError jsonError = deserializeJson(document, payload);

  if (jsonError) {
    showStatus("JSON ERROR", jsonError.c_str(), LCARS_RED);
    Serial.printf("ERROR: JSON parsing failed: %s\n", jsonError.c_str());
    return false;
  }

  JsonObject current = document["current"].as<JsonObject>();
  JsonObject daily = document["daily"].as<JsonObject>();

  if (current.isNull() || daily.isNull()) {
    showStatus("DATA ERROR", "CURRENT OR DAILY DATA IS MISSING.", LCARS_RED);
    Serial.println("ERROR: Required JSON objects are missing");
    return false;
  }

  float temperature = current["temperature_2m"].as<float>();
  float apparentTemperature = current["apparent_temperature"].as<float>();
  int humidity = current["relative_humidity_2m"].as<int>();
  int weatherCode = current["weather_code"].as<int>();
  float windSpeed = current["wind_speed_10m"].as<float>();
  String observationTime = current["time"].as<String>();

  float highTemperature = daily["temperature_2m_max"][0].as<float>();
  float lowTemperature = daily["temperature_2m_min"][0].as<float>();
  int rainChance = daily["precipitation_probability_max"][0].as<int>();
  String lastCheckedTime = getLastCheckedTime();

  Serial.printf("Open-Meteo observation time: %s\n", observationTime.c_str());
  Serial.printf("Last checked at %s\n", lastCheckedTime.c_str());

  drawWeatherScreen(
    temperature,
    apparentTemperature,
    humidity,
    weatherCode,
    windSpeed,
    highTemperature,
    lowTemperature,
    rainChance,
    lastCheckedTime
  );

  Serial.println("Weather data parsed and displayed");
  return true;
}

void updateWeather() {
  bool succeeded = fetchAndDisplayWeather();
  weatherAttemptInterval = succeeded
    ? WEATHER_REFRESH_INTERVAL_MS
    : WEATHER_RETRY_INTERVAL_MS;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("MPD Weather starting");

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!TCA.begin()) {
    stopWithError("ERROR: TCA9554 not detected");
  }

  if (!TCA.pinMode1(1, OUTPUT)) {
    stopWithError("ERROR: Could not configure LCD reset pin");
  }

  lcdReset();

  if (!gfx->begin(40000000, SPI_MODE0)) {
    stopWithError("ERROR: Display initialization failed");
  }

  digitalWrite(GFX_BL, HIGH);

  if (connectToWiFi()) {
    updateWeather();
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastConnectionAttempt >= WIFI_RETRY_INTERVAL_MS) {
      if (connectToWiFi()) {
        updateWeather();
      }
    }
  } else if (millis() - lastWeatherAttempt >= weatherAttemptInterval) {
    updateWeather();
  }

  updateHeaderClock();

  delay(250);
}
