#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"
#include "MPDAAFonts.h"
#include "StockConfig.h"
#include "TCCChannelQR.h"
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

constexpr uint16_t SCREEN_WIDTH = 480;
constexpr uint16_t SCREEN_HEIGHT = 320;
constexpr uint16_t COLOR_BACKGROUND = RGB565(232, 232, 234);
constexpr uint16_t COLOR_TEXT = RGB565(26, 28, 31);
constexpr uint16_t COLOR_HEADER = RGB565(51, 156, 255);
constexpr uint16_t COLOR_PANEL = RGB565(211, 211, 213);
constexpr uint16_t COLOR_CHART_AREA = RGB565(185, 202, 220);
constexpr uint16_t COLOR_GRID = RGB565(178, 195, 212);
constexpr uint16_t COLOR_METRIC = RGB565(229, 242, 255);
constexpr uint16_t COLOR_WHITE = RGB565(255, 255, 255);
constexpr uint16_t COLOR_MUTED = RGB565(130, 131, 133);
constexpr uint16_t COLOR_CYAN = RGB565(51, 156, 255);
constexpr uint16_t COLOR_GREEN = RGB565(18, 145, 82);
constexpr uint16_t COLOR_RED = RGB565(210, 65, 65);
constexpr uint16_t COLOR_GOLD = RGB565(237, 177, 49);
constexpr uint16_t COLOR_NEWS = RGB565(51, 156, 255);

constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long QUOTE_REFRESH_INTERVAL_MS = 30UL * 60UL * 1000UL;
constexpr unsigned long CHART_REFRESH_INTERVAL_MS = 2UL * 60UL * 60UL * 1000UL;
constexpr unsigned long CHART_RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long NEWS_REFRESH_INTERVAL_MS = 3UL * 60UL * 60UL * 1000UL;
constexpr unsigned long EOD_REFRESH_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;

constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.nist.gov";
constexpr char MARKET_TIME_ZONE[] = "EST5EDT,M3.2.0/2,M11.1.0/2";

constexpr int16_t AA_TEXT_BUFFER_WIDTH = 480;
constexpr int16_t AA_TEXT_BUFFER_HEIGHT = 64;
constexpr int16_t HEADER_CLOCK_X = 185;
constexpr int16_t HEADER_CLOCK_Y = 8;
constexpr int16_t HEADER_CLOCK_WIDTH = 110;
constexpr int16_t HEADER_CLOCK_HEIGHT = 48;
constexpr int16_t HEADER_TEXT_BASELINE = 38;

constexpr int16_t CHART_PANEL_X = 8;
constexpr int16_t CHART_PANEL_Y = 114;
constexpr int16_t CHART_PANEL_WIDTH = 464;
constexpr int16_t CHART_PANEL_HEIGHT = 88;
constexpr int16_t CHART_X = 16;
constexpr int16_t CHART_Y = 154;
constexpr int16_t CHART_WIDTH = 448;
constexpr int16_t CHART_HEIGHT = 48;
constexpr int16_t RANGE_BUTTON_Y = 120;
constexpr int16_t RANGE_BUTTON_WIDTH = 34;
constexpr int16_t RANGE_BUTTON_HEIGHT = 28;
constexpr int16_t RANGE_BUTTON_1D_X = 350;
constexpr int16_t RANGE_BUTTON_5D_X = 389;
constexpr int16_t RANGE_BUTTON_1M_X = 428;

constexpr int16_t LOGO_TOUCH_WIDTH = 96;
constexpr int16_t LOGO_TOUCH_HEIGHT = 96;
constexpr int16_t RANGE_TOUCH_Y = 106;
constexpr int16_t RANGE_TOUCH_HEIGHT = 56;
constexpr int16_t RANGE_TOUCH_1D_X = 330;
constexpr int16_t RANGE_TOUCH_1D_WIDTH = 56;
constexpr int16_t RANGE_TOUCH_5D_X = 386;
constexpr int16_t RANGE_TOUCH_5D_WIDTH = 39;
constexpr int16_t RANGE_TOUCH_1M_X = 425;
constexpr int16_t RANGE_TOUCH_1M_WIDTH = 55;
constexpr unsigned long TOUCH_NEW_PRESS_RECOVERY_MS = 50;
constexpr int16_t QR_MODAL_X =
  (SCREEN_WIDTH - TCC_CHANNEL_QR_PIXEL_SIZE) / 2;
constexpr int16_t QR_MODAL_Y = 2;
constexpr int16_t QR_MODAL_MESSAGE_Y = 268;
constexpr int16_t QR_MODAL_MESSAGE_HEIGHT = 52;

constexpr size_t MAX_CHART_POINTS = 96;
constexpr size_t MAX_HEADLINE_LENGTH = 160;
constexpr size_t SCREENSHOT_BYTE_COUNT =
  static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
constexpr size_t SCREENSHOT_SERIAL_CHUNK_SIZE = 4096;
constexpr char SCREENSHOT_COMMAND[] = "MPD_SCREENSHOT";

TCA9554 TCA(0x20);
TouchDrvFT6X36 touch;

Arduino_DataBus* bus = new Arduino_ESP32SPI(
  LCD_DC,
  LCD_CS,
  SPI_SCLK,
  SPI_MOSI,
  SPI_MISO
);

Arduino_ST7796* display = new Arduino_ST7796(
  bus,
  LCD_RST,
  1,
  true,
  LCD_HOR_RES,
  LCD_VER_RES
);
Arduino_GFX* gfx = display;

enum ChartRange : uint8_t {
  RANGE_1D,
  RANGE_5D,
  RANGE_1M
};

enum TouchReadState : uint8_t {
  TOUCH_READ_ERROR,
  TOUCH_RELEASED,
  TOUCH_PRESSED
};

struct StockQuote {
  bool valid = false;
  bool averageVolumeValid = false;
  char ticker[12] = "";
  char name[72] = "";
  char exchange[20] = "US";
  char lastTradeDate[11] = "";
  float price = 0.0f;
  float open = 0.0f;
  float previousClose = 0.0f;
  float dayHigh = 0.0f;
  float dayLow = 0.0f;
  uint32_t volume = 0;
  uint32_t averageVolume = 0;
};

struct ChartSeries {
  bool valid = false;
  size_t count = 0;
  float points[MAX_CHART_POINTS];
};

struct NewsSnapshot {
  bool valid = false;
  uint8_t count = 0;
  char headlines[2][MAX_HEADLINE_LENGTH] = {{0}, {0}};
};

struct AATextBounds {
  int16_t left;
  int16_t top;
  uint16_t width;
  uint16_t height;
};

StockQuote latestQuote;
ChartSeries chart1D;
ChartSeries chart5D;
ChartSeries chart1M;
NewsSnapshot latestNews;
ChartRange selectedRange = RANGE_1D;

uint16_t aaTextBuffer[AA_TEXT_BUFFER_WIDTH * AA_TEXT_BUFFER_HEIGHT];
uint16_t chartPlotBuffer[CHART_WIDTH * CHART_HEIGHT];
char serialCommandBuffer[32] = "";
size_t serialCommandLength = 0;
char lastHeaderClock[12] = "";

bool touchAvailable = false;
bool touchWasDown = false;
bool qrModalVisible = false;
bool clockConfigured = false;

unsigned long lastConnectionAttempt = 0;
unsigned long lastQuoteAttempt = 0;
unsigned long lastChartAttempt = 0;
unsigned long lastRangeFetchAttempt[3] = {0, 0, 0};
unsigned long lastNewsAttempt = 0;
unsigned long lastEodAttempt = 0;
unsigned long lastHeaderClockRefresh = 0;
unsigned long lastTouchActionAt = 0;

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

void sanitizeText(const char* source, char* destination, size_t destinationSize) {
  if (destinationSize == 0) return;

  size_t output = 0;
  bool previousWasSpace = false;

  for (const uint8_t* cursor = reinterpret_cast<const uint8_t*>(source);
       *cursor != 0 && output < destinationSize - 1;
       ++cursor) {
    uint8_t character = *cursor;

    if (character >= 0x80) {
      continue;
    }

    if (character == '\r' || character == '\n' || character == '\t') {
      character = ' ';
    }

    if (character < 0x20 || character > 0x7E) {
      continue;
    }

    if (character == ' ') {
      if (previousWasSpace || output == 0) {
        continue;
      }
      previousWasSpace = true;
    } else {
      previousWasSpace = false;
    }

    destination[output++] = static_cast<char>(character);
  }

  while (output > 0 && destination[output - 1] == ' ') {
    --output;
  }
  destination[output] = '\0';
}

void uppercaseAscii(char* text) {
  for (char* cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor >= 'a' && *cursor <= 'z') {
      *cursor = *cursor - 'a' + 'A';
    }
  }
}

void fitTextToWidth(
  const MPDAAFont& font,
  const char* source,
  char* destination,
  size_t destinationSize,
  uint16_t maximumWidth
) {
  snprintf(destination, destinationSize, "%s", source);
  if (measureAAText(font, destination).width <= maximumWidth) {
    return;
  }

  size_t length = strlen(destination);
  while (length > 3) {
    destination[--length] = '\0';
    snprintf(destination + length, destinationSize - length, "...");
    if (measureAAText(font, destination).width <= maximumWidth) {
      return;
    }
    destination[length] = '\0';
  }

  snprintf(destination, destinationSize, "...");
}

void formatVolume(uint32_t volume, char* destination, size_t destinationSize) {
  if (volume >= 1000000000UL) {
    snprintf(destination, destinationSize, "%.1fB", volume / 1000000000.0f);
  } else if (volume >= 1000000UL) {
    snprintf(destination, destinationSize, "%.1fM", volume / 1000000.0f);
  } else if (volume >= 1000UL) {
    snprintf(destination, destinationSize, "%.1fK", volume / 1000.0f);
  } else {
    snprintf(destination, destinationSize, "%lu", static_cast<unsigned long>(volume));
  }
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
    COLOR_HEADER
  );
  AATextBounds bounds = measureAAText(MPDFontHeaderAA, clockText);
  int16_t cursorX = HEADER_CLOCK_X +
    ((HEADER_CLOCK_WIDTH - bounds.width) / 2) - bounds.left;
  drawAAText(
    MPDFontHeaderAA,
    clockText,
    cursorX,
    HEADER_TEXT_BASELINE,
    COLOR_WHITE,
    COLOR_HEADER
  );
  snprintf(lastHeaderClock, sizeof(lastHeaderClock), "%s", clockText);
}

const char* displayedTicker() {
  return latestQuote.valid && latestQuote.ticker[0] != '\0'
    ? latestQuote.ticker
    : STOCK_SYMBOL;
}

void drawHeader() {
  gfx->fillRect(8, 8, 464, 48, COLOR_HEADER);

  static_cast<Arduino_GFX*>(gfx)->draw16bitRGBBitmapWithMask(
    9,
    9,
    TCCLogo46pxPixels,
    TCCLogo46pxMask,
    TCC_LOGO_WIDTH,
    TCC_LOGO_HEIGHT
  );

  drawAAText(
    MPDFontHeaderBoldAA,
    "MPD MARKETS",
    60,
    HEADER_TEXT_BASELINE,
    COLOR_WHITE,
    COLOR_HEADER
  );
  drawAARightAligned(
    MPDFontHeaderBoldAA,
    displayedTicker(),
    460,
    HEADER_TEXT_BASELINE,
    COLOR_WHITE,
    COLOR_HEADER
  );
  updateHeaderClock(true);
}

void showStatus(const char* title, const char* detail, uint16_t color) {
  if (qrModalVisible) {
    return;
  }

  gfx->fillScreen(COLOR_BACKGROUND);
  drawHeader();
  gfx->fillRoundRect(36, 106, 408, 58, 16, color);
  drawAACenteredInRect(
    MPDFontMediumAA,
    title,
    36,
    106,
    408,
    58,
    COLOR_TEXT,
    color
  );
  drawAACenteredInRect(
    MPDFontSmallAA,
    detail,
    24,
    184,
    432,
    42,
    COLOR_MUTED,
    COLOR_BACKGROUND
  );
}

void drawRangeButton(
  int16_t x,
  const char* label,
  ChartRange range
) {
  bool selected = selectedRange == range;
  uint16_t background = selected ? COLOR_TEXT : COLOR_WHITE;
  uint16_t foreground = selected ? COLOR_WHITE : COLOR_TEXT;
  gfx->fillRoundRect(
    x,
    RANGE_BUTTON_Y,
    RANGE_BUTTON_WIDTH,
    RANGE_BUTTON_HEIGHT,
    12,
    background
  );
  drawAACenteredInRect(
    MPDFontSmallAA,
    label,
    x,
    RANGE_BUTTON_Y,
    RANGE_BUTTON_WIDTH,
    RANGE_BUTTON_HEIGHT,
    foreground,
    background
  );
}

ChartSeries& selectedSeries() {
  if (selectedRange == RANGE_5D) return chart5D;
  if (selectedRange == RANGE_1M) return chart1M;
  return chart1D;
}

const char* selectedRangeLabel() {
  if (selectedRange == RANGE_5D) return "FIVE DAYS / 5D";
  if (selectedRange == RANGE_1M) return "ONE MONTH / 1M";
  return "TODAY / 1D";
}

void clearChartPlot(uint16_t color) {
  constexpr size_t pixelCount =
    static_cast<size_t>(CHART_WIDTH) * CHART_HEIGHT;
  for (size_t index = 0; index < pixelCount; ++index) {
    chartPlotBuffer[index] = color;
  }
}

void setChartPlotPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= CHART_WIDTH || y < 0 || y >= CHART_HEIGHT) {
    return;
  }
  chartPlotBuffer[(static_cast<size_t>(y) * CHART_WIDTH) + x] = color;
}

void drawChartPlotLine(
  int16_t x0,
  int16_t y0,
  int16_t x1,
  int16_t y1,
  uint16_t color
) {
  int16_t deltaX = abs(x1 - x0);
  int16_t stepX = x0 < x1 ? 1 : -1;
  int16_t deltaY = -abs(y1 - y0);
  int16_t stepY = y0 < y1 ? 1 : -1;
  int16_t error = deltaX + deltaY;

  while (true) {
    setChartPlotPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }

    int16_t doubledError = error * 2;
    if (doubledError >= deltaY) {
      error += deltaY;
      x0 += stepX;
    }
    if (doubledError <= deltaX) {
      error += deltaX;
      y0 += stepY;
    }
  }
}

void drawChartPlotDot(int16_t centerX, int16_t centerY) {
  for (int16_t y = -4; y <= 4; ++y) {
    for (int16_t x = -4; x <= 4; ++x) {
      int16_t distanceSquared = (x * x) + (y * y);
      if (distanceSquared <= 16) {
        setChartPlotPixel(centerX + x, centerY + y, COLOR_CYAN);
      }
      if (distanceSquared <= 4) {
        setChartPlotPixel(centerX + x, centerY + y, COLOR_METRIC);
      }
    }
  }
}

void drawChartPlotBitmap() {
  gfx->draw16bitRGBBitmap(
    CHART_X,
    CHART_Y,
    chartPlotBuffer,
    CHART_WIDTH,
    CHART_HEIGHT
  );
}

void drawChart() {
  gfx->fillRect(
    CHART_PANEL_X,
    CHART_PANEL_Y,
    CHART_PANEL_WIDTH,
    CHART_PANEL_HEIGHT,
    COLOR_PANEL
  );

  drawAAText(
    MPDFontSmallBoldAA,
    selectedRangeLabel(),
    16,
    141,
    COLOR_MUTED,
    COLOR_PANEL
  );

  drawRangeButton(RANGE_BUTTON_1D_X, "1D", RANGE_1D);
  drawRangeButton(RANGE_BUTTON_5D_X, "5D", RANGE_5D);
  drawRangeButton(RANGE_BUTTON_1M_X, "1M", RANGE_1M);

  clearChartPlot(COLOR_PANEL);
  ChartSeries& series = selectedSeries();
  if (!series.valid || series.count < 2) {
    for (uint8_t line = 0; line < 3; ++line) {
      int16_t y = line * ((CHART_HEIGHT - 1) / 2);
      for (int16_t x = 0; x < CHART_WIDTH; ++x) {
        setChartPlotPixel(x, y, COLOR_GRID);
      }
    }
    drawChartPlotBitmap();
    drawAACenteredInRect(
      MPDFontSmallAA,
      "CHART DATA UNAVAILABLE",
      CHART_X,
      CHART_Y,
      CHART_WIDTH,
      CHART_HEIGHT,
      COLOR_MUTED,
      COLOR_PANEL
    );
    return;
  }

  float low = series.points[0];
  float high = series.points[0];
  for (size_t index = 1; index < series.count; ++index) {
    low = min(low, series.points[index]);
    high = max(high, series.points[index]);
  }

  float spread = max(high - low, 0.01f);
  int16_t previousX = 0;
  int16_t previousY = CHART_HEIGHT - 1 -
    static_cast<int16_t>(
      ((series.points[0] - low) / spread) * (CHART_HEIGHT - 1)
    );

  int16_t pointX[MAX_CHART_POINTS];
  int16_t pointY[MAX_CHART_POINTS];
  pointX[0] = previousX;
  pointY[0] = previousY;

  for (size_t index = 1; index < series.count; ++index) {
    int16_t x = static_cast<int16_t>(
      (index * (CHART_WIDTH - 1)) / (series.count - 1)
    );
    int16_t y = CHART_HEIGHT - 1 -
      static_cast<int16_t>(
        ((series.points[index] - low) / spread) * (CHART_HEIGHT - 1)
      );
    pointX[index] = x;
    pointY[index] = y;

    int16_t segmentWidth = x - previousX;
    if (segmentWidth < 1) {
      segmentWidth = 1;
    }
    for (int16_t column = previousX; column <= x; ++column) {
      float progress = static_cast<float>(column - previousX) / segmentWidth;
      int16_t fillY = previousY +
        static_cast<int16_t>((y - previousY) * progress);
      for (int16_t row = fillY; row < CHART_HEIGHT; ++row) {
        setChartPlotPixel(column, row, COLOR_CHART_AREA);
      }
    }
    previousX = x;
    previousY = y;
  }

  for (uint8_t line = 0; line < 3; ++line) {
    int16_t y = line * ((CHART_HEIGHT - 1) / 2);
    for (int16_t x = 0; x < CHART_WIDTH; ++x) {
      setChartPlotPixel(x, y, COLOR_GRID);
    }
  }

  for (size_t index = 1; index < series.count; ++index) {
    drawChartPlotLine(
      pointX[index - 1],
      pointY[index - 1],
      pointX[index],
      pointY[index],
      COLOR_CYAN
    );
    drawChartPlotLine(
      pointX[index - 1],
      pointY[index - 1] + 1,
      pointX[index],
      pointY[index] + 1,
      COLOR_CYAN
    );
  }

  drawChartPlotDot(previousX, previousY);
  drawChartPlotBitmap();
}

void drawMetricCard(
  int16_t x,
  int16_t width,
  uint16_t color,
  const char* label,
  const char* value
) {
  constexpr int16_t y = 206;
  constexpr int16_t height = 46;
  gfx->fillRoundRect(x, y, width, height, height / 2, color);

  drawAACenteredInRect(
    MPDFontSmallAA,
    label,
    x,
    y + 2,
    width,
    19,
    COLOR_CYAN,
    color
  );

  drawAACenteredInRect(
    MPDFontSmallBoldAA,
    value,
    x,
    y + 19,
    width,
    height - 20,
    COLOR_CYAN,
    color
  );
}

void drawMetrics() {
  char openText[18];
  char previousCloseText[18];
  char dayRangeText[32];
  char volumeText[18];
  char averageVolumeText[18];

  snprintf(openText, sizeof(openText), "$%.2f", latestQuote.open);
  snprintf(
    previousCloseText,
    sizeof(previousCloseText),
    "$%.2f",
    latestQuote.previousClose
  );
  snprintf(
    dayRangeText,
    sizeof(dayRangeText),
    "$%.2f-%.2f",
    latestQuote.dayLow,
    latestQuote.dayHigh
  );
  formatVolume(latestQuote.volume, volumeText, sizeof(volumeText));

  if (latestQuote.averageVolumeValid) {
    formatVolume(
      latestQuote.averageVolume,
      averageVolumeText,
      sizeof(averageVolumeText)
    );
  } else {
    snprintf(averageVolumeText, sizeof(averageVolumeText), "--");
  }

  drawMetricCard(8, 75, COLOR_METRIC, "OPEN", openText);
  drawMetricCard(87, 95, COLOR_METRIC, "PREV CLOSE", previousCloseText);
  drawMetricCard(186, 127, COLOR_METRIC, "DAY RANGE", dayRangeText);
  drawMetricCard(317, 75, COLOR_METRIC, "VOLUME", volumeText);
  drawMetricCard(396, 76, COLOR_METRIC, "AVG VOL", averageVolumeText);
}

void drawNews() {
  constexpr int16_t x = 8;
  constexpr int16_t y = 256;
  constexpr int16_t width = 464;
  constexpr int16_t height = 56;
  constexpr int16_t labelWidth = 64;

  gfx->fillRect(x, y, width, height, COLOR_NEWS);

  drawAACenteredInRect(
    MPDFontHeaderBoldAA,
    "LATEST",
    x + 4,
    y,
    labelWidth,
    height,
    COLOR_WHITE,
    COLOR_NEWS
  );

  char firstLine[MAX_HEADLINE_LENGTH + 4];
  char secondLine[MAX_HEADLINE_LENGTH + 4];

  if (latestNews.valid && latestNews.count > 0) {
    snprintf(firstLine, sizeof(firstLine), "01 %s", latestNews.headlines[0]);
  } else {
    snprintf(firstLine, sizeof(firstLine), "01 NEWS UNAVAILABLE");
  }

  if (latestNews.valid && latestNews.count > 1) {
    snprintf(secondLine, sizeof(secondLine), "02 %s", latestNews.headlines[1]);
  } else {
    snprintf(secondLine, sizeof(secondLine), "02 NO SECOND HEADLINE RETURNED");
  }

  char fittedFirst[MAX_HEADLINE_LENGTH + 4];
  char fittedSecond[MAX_HEADLINE_LENGTH + 4];
  fitTextToWidth(
    MPDFontSmallAA,
    firstLine,
    fittedFirst,
    sizeof(fittedFirst),
    width - labelWidth - 26
  );
  fitTextToWidth(
    MPDFontSmallAA,
    secondLine,
    fittedSecond,
    sizeof(fittedSecond),
    width - labelWidth - 26
  );

  drawAAText(
    MPDFontSmallAA,
    fittedFirst,
    x + labelWidth + 16,
    y + 20,
    COLOR_WHITE,
    COLOR_NEWS
  );
  drawAAText(
    MPDFontSmallAA,
    fittedSecond,
    x + labelWidth + 16,
    y + 43,
    COLOR_WHITE,
    COLOR_NEWS
  );
}

void drawQuoteSummary() {
  char companyName[72];
  snprintf(companyName, sizeof(companyName), "%s", latestQuote.name);
  uppercaseAscii(companyName);

  char fittedCompanyName[72];
  fitTextToWidth(
    MPDFontHeaderBoldAA,
    companyName,
    fittedCompanyName,
    sizeof(fittedCompanyName),
    144
  );

  char priceText[24];
  snprintf(priceText, sizeof(priceText), "$%.2f", latestQuote.price);

  float change = latestQuote.price - latestQuote.open;
  float percent = latestQuote.open != 0.0f
    ? (change / latestQuote.open) * 100.0f
    : 0.0f;
  char changeText[32];
  snprintf(changeText, sizeof(changeText), "%+.2f  %+.2f%%", change, percent);
  uint16_t changeColor = COLOR_TEXT;
  if (change > 0.005f) {
    changeColor = COLOR_GREEN;
  } else if (change < -0.005f) {
    changeColor = COLOR_RED;
  }

  char marketLine[40];
  snprintf(
    marketLine,
    sizeof(marketLine),
    "%s / MARKET DATA",
    latestQuote.exchange
  );

  gfx->fillRect(8, 60, 464, 50, COLOR_BACKGROUND);

  drawAAText(
    MPDFontHeaderBoldAA,
    fittedCompanyName,
    16,
    82,
    COLOR_TEXT,
    COLOR_BACKGROUND
  );
  drawAAText(
    MPDFontSmallAA,
    marketLine,
    16,
    104,
    COLOR_MUTED,
    COLOR_BACKGROUND
  );

  drawAACenteredInRect(
    MPDFontMediumAA,
    priceText,
    158,
    60,
    164,
    50,
    COLOR_TEXT,
    COLOR_BACKGROUND
  );

  drawAARightAligned(
    MPDFontHeaderBoldAA,
    changeText,
    464,
    82,
    changeColor,
    COLOR_BACKGROUND
  );
  drawAARightAligned(
    MPDFontSmallAA,
    "FROM OPEN",
    464,
    104,
    COLOR_MUTED,
    COLOR_BACKGROUND
  );
}

void drawStockScreen() {
  if (!latestQuote.valid) {
    showStatus("STOCK DATA UNAVAILABLE", "WAITING FOR A SUCCESSFUL UPDATE", COLOR_RED);
    return;
  }

  gfx->fillScreen(COLOR_BACKGROUND);
  drawHeader();
  drawQuoteSummary();
  drawChart();
  drawMetrics();
  drawNews();
}

void drawChannelQrCode(int16_t x, int16_t y) {
  gfx->fillRect(
    x,
    y,
    TCC_CHANNEL_QR_PIXEL_SIZE,
    TCC_CHANNEL_QR_PIXEL_SIZE,
    RGB565_WHITE
  );

  const int16_t firstModuleX = x +
    (TCC_CHANNEL_QR_QUIET_ZONE * TCC_CHANNEL_QR_MODULE_SCALE);
  const int16_t firstModuleY = y +
    (TCC_CHANNEL_QR_QUIET_ZONE * TCC_CHANNEL_QR_MODULE_SCALE);

  for (uint8_t row = 0; row < TCC_CHANNEL_QR_MODULE_COUNT; ++row) {
    uint32_t rowBits = pgm_read_dword(&TCCChannelQRRows[row]);
    int8_t runStart = -1;

    for (uint8_t column = 0; column <= TCC_CHANNEL_QR_MODULE_COUNT; ++column) {
      bool moduleIsBlack = false;
      if (column < TCC_CHANNEL_QR_MODULE_COUNT) {
        uint8_t bitIndex = TCC_CHANNEL_QR_MODULE_COUNT - 1 - column;
        moduleIsBlack = (rowBits & (1UL << bitIndex)) != 0;
      }

      if (moduleIsBlack && runStart < 0) {
        runStart = column;
      } else if (!moduleIsBlack && runStart >= 0) {
        gfx->fillRect(
          firstModuleX + (runStart * TCC_CHANNEL_QR_MODULE_SCALE),
          firstModuleY + (row * TCC_CHANNEL_QR_MODULE_SCALE),
          (column - runStart) * TCC_CHANNEL_QR_MODULE_SCALE,
          TCC_CHANNEL_QR_MODULE_SCALE,
          RGB565_BLACK
        );
        runStart = -1;
      }
    }
  }
}

void drawChannelQrModal() {
  gfx->fillScreen(RGB565_BLACK);
  drawChannelQrCode(QR_MODAL_X, QR_MODAL_Y);
  drawAACenteredInRect(
    MPDFontSmallAA,
    "TAP ANYWHERE TO DISMISS",
    0,
    QR_MODAL_MESSAGE_Y,
    SCREEN_WIDTH,
    QR_MODAL_MESSAGE_HEIGHT,
    COLOR_WHITE,
    RGB565_BLACK
  );
}

bool credentialsAreConfigured() {
  return strcmp(WIFI_SSID, "YOUR_WIFI_NAME") != 0 &&
    strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0 &&
    strcmp(STOCKDATA_API_TOKEN, "YOUR_STOCKDATA_API_TOKEN") != 0 &&
    strlen(STOCKDATA_API_TOKEN) > 0;
}

bool connectToWiFi() {
  lastConnectionAttempt = millis();

  if (!credentialsAreConfigured()) {
    showStatus("SETUP REQUIRED", "UPDATE THE LOCAL SECRETS.H FILE", COLOR_GOLD);
    Serial.println("ERROR: Wi-Fi or StockData credentials are not configured");
    return false;
  }

  showStatus("CONNECTING TO WI-FI", WIFI_SSID, COLOR_CYAN);
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WI-FI FAILED", "CHECK SECRETS.H AND TRY AGAIN", COLOR_RED);
    Serial.println("ERROR: Wi-Fi connection timed out");
    return false;
  }

  Serial.print("Wi-Fi connected. IP: ");
  Serial.println(WiFi.localIP());

  configTzTime(MARKET_TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);
  clockConfigured = true;
  lastHeaderClock[0] = '\0';
  return true;
}

bool beginStockRequest(
  WiFiClientSecure& secureClient,
  HTTPClient& http,
  const char* url,
  const char* requestName,
  int& httpCode
) {
  secureClient.setInsecure();
  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  if (!http.begin(secureClient, url)) {
    Serial.printf("ERROR: Could not start %s request\n", requestName);
    return false;
  }

  Serial.printf("Requesting %s for %s (API token hidden)\n", requestName, STOCK_SYMBOL);
  httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf(
      "ERROR: StockData.org %s request returned HTTP %d\n",
      requestName,
      httpCode
    );
    http.end();
    return false;
  }

  return true;
}

bool readStockJson(
  HTTPClient& http,
  JsonDocument& document,
  const char* responseName
) {
  String payload = http.getString();
  http.end();

  if (payload.length() == 0) {
    Serial.printf("ERROR: %s response body was empty\n", responseName);
    return false;
  }

  Serial.printf(
    "%s response received: %u bytes\n",
    responseName,
    static_cast<unsigned int>(payload.length())
  );

  DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.printf(
      "ERROR: %s JSON parsing failed: %s\n",
      responseName,
      error.c_str()
    );
    return false;
  }

  return true;
}

bool fetchQuote() {
  lastQuoteAttempt = millis();

  char url[384];
  int length = snprintf(
    url,
    sizeof(url),
    "https://api.stockdata.org/v1/data/quote"
    "?symbols=%s"
    "&api_token=%s",
    STOCK_SYMBOL,
    STOCKDATA_API_TOKEN
  );
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(url)) {
    Serial.println("ERROR: Quote URL is too long");
    return false;
  }

  WiFiClientSecure secureClient;
  HTTPClient http;
  int httpCode = 0;
  if (!beginStockRequest(secureClient, http, url, "quote", httpCode)) {
    return false;
  }

  JsonDocument document;
  if (!readStockJson(http, document, "Quote")) {
    return false;
  }

  JsonObject item = document["data"][0].as<JsonObject>();
  if (item.isNull()) {
    Serial.println("ERROR: Quote response contained no data");
    return false;
  }

  char cleanName[72];
  sanitizeText(item["name"] | "Unknown company", cleanName, sizeof(cleanName));
  char cleanExchange[20];
  sanitizeText(item["exchange_short"] | "IEX", cleanExchange, sizeof(cleanExchange));

  snprintf(latestQuote.ticker, sizeof(latestQuote.ticker), "%s", item["ticker"] | STOCK_SYMBOL);
  snprintf(latestQuote.name, sizeof(latestQuote.name), "%s", cleanName);
  snprintf(latestQuote.exchange, sizeof(latestQuote.exchange), "%s", cleanExchange);
  latestQuote.price = item["price"] | 0.0f;
  latestQuote.open = item["day_open"] | 0.0f;
  latestQuote.previousClose = item["previous_close_price"] | 0.0f;
  latestQuote.dayHigh = item["day_high"] | 0.0f;
  latestQuote.dayLow = item["day_low"] | 0.0f;
  latestQuote.volume = item["volume"] | 0U;

  const char* lastTradeTime = item["last_trade_time"] | "";
  if (strlen(lastTradeTime) >= 10) {
    memcpy(latestQuote.lastTradeDate, lastTradeTime, 10);
    latestQuote.lastTradeDate[10] = '\0';
  }

  latestQuote.valid = latestQuote.price > 0.0f;

  Serial.printf(
    "Quote received: %s %s $%.2f, session %s\n",
    latestQuote.ticker,
    latestQuote.name,
    latestQuote.price,
    latestQuote.lastTradeDate
  );
  return latestQuote.valid;
}

bool subtractDays(
  const char* isoDate,
  int days,
  char* destination,
  size_t destinationSize
) {
  int year;
  int month;
  int day;
  if (sscanf(isoDate, "%d-%d-%d", &year, &month, &day) != 3) {
    return false;
  }

  struct tm date = {};
  date.tm_year = year - 1900;
  date.tm_mon = month - 1;
  date.tm_mday = day;
  date.tm_hour = 12;
  date.tm_isdst = -1;

  time_t timestamp = mktime(&date);
  if (timestamp == static_cast<time_t>(-1)) {
    return false;
  }

  timestamp -= static_cast<time_t>(days) * 24 * 60 * 60;
  struct tm result;
  localtime_r(&timestamp, &result);
  return strftime(destination, destinationSize, "%Y-%m-%d", &result) > 0;
}

float eodClose(JsonObject entry) {
  if (!entry["close"].isNull()) {
    return entry["close"].as<float>();
  }
  return entry["data"]["close"] | 0.0f;
}

uint32_t eodVolume(JsonObject entry) {
  if (!entry["volume"].isNull()) {
    return entry["volume"].as<uint32_t>();
  }
  return entry["data"]["volume"] | 0U;
}

bool fetchEodHistory() {
  lastEodAttempt = millis();

  if (latestQuote.lastTradeDate[0] == '\0') {
    Serial.println("ERROR: Cannot request EOD history without a trade date");
    return false;
  }

  char dateFrom[11];
  if (!subtractDays(latestQuote.lastTradeDate, 30, dateFrom, sizeof(dateFrom))) {
    Serial.println("ERROR: Could not calculate EOD start date");
    return false;
  }

  char url[512];
  int length = snprintf(
    url,
    sizeof(url),
    "https://api.stockdata.org/v1/data/eod"
    "?symbols=%s"
    "&interval=day"
    "&sort=asc"
    "&date_from=%s"
    "&date_to=%s"
    "&api_token=%s",
    STOCK_SYMBOL,
    dateFrom,
    latestQuote.lastTradeDate,
    STOCKDATA_API_TOKEN
  );
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(url)) {
    Serial.println("ERROR: EOD URL is too long");
    return false;
  }

  WiFiClientSecure secureClient;
  HTTPClient http;
  int httpCode = 0;
  if (!beginStockRequest(secureClient, http, url, "end-of-day history", httpCode)) {
    return false;
  }

  JsonDocument document;
  if (!readStockJson(http, document, "EOD history")) {
    return false;
  }

  JsonArray data = document["data"].as<JsonArray>();
  if (data.isNull() || data.size() == 0) {
    Serial.println("ERROR: EOD response contained no data");
    return false;
  }

  chart1M.count = 0;
  for (JsonObject entry : data) {
    float close = eodClose(entry);
    if (close <= 0.0f) continue;

    if (chart1M.count < MAX_CHART_POINTS) {
      chart1M.points[chart1M.count++] = close;
    }
  }
  chart1M.valid = chart1M.count >= 2;

  size_t validVolumes = 0;
  for (JsonObject entry : data) {
    if (eodVolume(entry) > 0) {
      ++validVolumes;
    }
  }

  size_t volumesToAverage = min(static_cast<size_t>(20), validVolumes);
  size_t volumesToSkip = validVolumes - volumesToAverage;
  size_t seen = 0;
  uint64_t volumeTotal = 0;

  for (JsonObject entry : data) {
    uint32_t volume = eodVolume(entry);
    if (volume == 0) continue;
    if (seen++ < volumesToSkip) continue;
    volumeTotal += volume;
  }

  if (volumesToAverage > 0) {
    latestQuote.averageVolume =
      static_cast<uint32_t>(volumeTotal / volumesToAverage);
    latestQuote.averageVolumeValid = true;
  }

  Serial.printf(
    "EOD history received: %u chart points, %u sessions in average volume\n",
    static_cast<unsigned int>(chart1M.count),
    static_cast<unsigned int>(volumesToAverage)
  );
  return chart1M.valid;
}

bool populateIntradaySeries(
  JsonArray data,
  ChartSeries& destination,
  bool keepOnlyLastFiveSessions
) {
  char sessionDates[12][11] = {{0}};
  size_t sessionCount = 0;

  if (keepOnlyLastFiveSessions) {
    for (JsonObject entry : data) {
      bool extendedHours = entry["data"]["is_extended_hours"] | false;
      float close = entry["data"]["close"] | 0.0f;
      const char* date = entry["date"] | "";
      if (extendedHours || close <= 0.0f || strlen(date) < 10) {
        continue;
      }

      if (
        sessionCount == 0 ||
        strncmp(sessionDates[sessionCount - 1], date, 10) != 0
      ) {
        if (sessionCount < 12) {
          memcpy(sessionDates[sessionCount], date, 10);
          sessionDates[sessionCount][10] = '\0';
          ++sessionCount;
        }
      }
    }
  }

  const char* firstIncludedSession = "";
  if (keepOnlyLastFiveSessions && sessionCount > 5) {
    firstIncludedSession = sessionDates[sessionCount - 5];
  }

  size_t validCount = 0;
  for (JsonObject entry : data) {
    bool extendedHours = entry["data"]["is_extended_hours"] | false;
    float close = entry["data"]["close"] | 0.0f;
    const char* date = entry["date"] | "";
    bool sessionIncluded =
      !keepOnlyLastFiveSessions ||
      firstIncludedSession[0] == '\0' ||
      (strlen(date) >= 10 && strncmp(date, firstIncludedSession, 10) >= 0);
    if (!extendedHours && close > 0.0f && sessionIncluded) {
      ++validCount;
    }
  }

  if (validCount < 2) {
    destination.valid = false;
    destination.count = 0;
    return false;
  }

  size_t outputCount = min(validCount, MAX_CHART_POINTS);
  size_t validIndex = 0;
  size_t outputIndex = 0;

  for (JsonObject entry : data) {
    bool extendedHours = entry["data"]["is_extended_hours"] | false;
    float close = entry["data"]["close"] | 0.0f;
    const char* date = entry["date"] | "";
    bool sessionIncluded =
      !keepOnlyLastFiveSessions ||
      firstIncludedSession[0] == '\0' ||
      (strlen(date) >= 10 && strncmp(date, firstIncludedSession, 10) >= 0);
    if (extendedHours || close <= 0.0f || !sessionIncluded) {
      continue;
    }

    size_t targetIndex = outputCount > 1
      ? (outputIndex * (validCount - 1)) / (outputCount - 1)
      : 0;

    if (validIndex == targetIndex && outputIndex < outputCount) {
      destination.points[outputIndex++] = close;
    }
    ++validIndex;
  }

  destination.count = outputIndex;
  destination.valid = outputIndex >= 2;
  return destination.valid;
}

bool fetchIntradayChart(ChartRange range) {
  lastRangeFetchAttempt[static_cast<uint8_t>(range)] = millis();

  if (range == RANGE_1M) {
    return chart1M.valid || fetchEodHistory();
  }

  if (latestQuote.lastTradeDate[0] == '\0') {
    Serial.println("ERROR: Cannot request intraday history without a trade date");
    return false;
  }

  char url[512];
  int length = 0;

  if (range == RANGE_1D) {
    length = snprintf(
      url,
      sizeof(url),
      "https://api.stockdata.org/v1/data/intraday"
      "?symbols=%s"
      "&interval=minute"
      "&sort=asc"
      "&date=%s"
      "&extended_hours=false"
      "&api_token=%s",
      STOCK_SYMBOL,
      latestQuote.lastTradeDate,
      STOCKDATA_API_TOKEN
    );
  } else {
    char dateFrom[11];
    if (!subtractDays(latestQuote.lastTradeDate, 10, dateFrom, sizeof(dateFrom))) {
      Serial.println("ERROR: Could not calculate 5D start date");
      return false;
    }

    length = snprintf(
      url,
      sizeof(url),
      "https://api.stockdata.org/v1/data/intraday"
      "?symbols=%s"
      "&interval=hour"
      "&sort=asc"
      "&date_from=%s"
      "&date_to=%s"
      "&extended_hours=false"
      "&api_token=%s",
      STOCK_SYMBOL,
      dateFrom,
      latestQuote.lastTradeDate,
      STOCKDATA_API_TOKEN
    );
  }

  if (length <= 0 || static_cast<size_t>(length) >= sizeof(url)) {
    Serial.println("ERROR: Intraday URL is too long");
    return false;
  }

  WiFiClientSecure secureClient;
  HTTPClient http;
  int httpCode = 0;
  const char* requestName = range == RANGE_1D ? "1D chart" : "5D chart";
  if (!beginStockRequest(secureClient, http, url, requestName, httpCode)) {
    return false;
  }

  JsonDocument document;
  if (!readStockJson(http, document, requestName)) {
    return false;
  }

  JsonArray data = document["data"].as<JsonArray>();
  if (data.isNull() || data.size() == 0) {
    Serial.printf("ERROR: %s response contained no data\n", requestName);
    return false;
  }

  ChartSeries& destination = range == RANGE_1D ? chart1D : chart5D;
  bool succeeded = populateIntradaySeries(
    data,
    destination,
    range == RANGE_5D
  );
  if (succeeded) {
    Serial.printf(
      "%s received: %u plotted points\n",
      requestName,
      static_cast<unsigned int>(destination.count)
    );
  }
  return succeeded;
}

bool fetchNews() {
  lastNewsAttempt = millis();

  char url[512];
  int length = snprintf(
    url,
    sizeof(url),
    "https://api.stockdata.org/v1/news/all"
    "?symbols=%s"
    "&language=en"
    "&filter_entities=true"
    "&group_similar=true"
    "&limit=2"
    "&api_token=%s",
    STOCK_SYMBOL,
    STOCKDATA_API_TOKEN
  );
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(url)) {
    Serial.println("ERROR: News URL is too long");
    return false;
  }

  WiFiClientSecure secureClient;
  HTTPClient http;
  int httpCode = 0;
  if (!beginStockRequest(secureClient, http, url, "news", httpCode)) {
    return false;
  }

  JsonDocument document;
  if (!readStockJson(http, document, "News")) {
    return false;
  }

  JsonArray data = document["data"].as<JsonArray>();
  latestNews.count = 0;

  for (JsonObject article : data) {
    if (latestNews.count >= 2) break;
    const char* title = article["title"] | "";
    if (title[0] == '\0') continue;

    sanitizeText(
      title,
      latestNews.headlines[latestNews.count],
      MAX_HEADLINE_LENGTH
    );
    if (latestNews.headlines[latestNews.count][0] != '\0') {
      ++latestNews.count;
    }
  }

  latestNews.valid = latestNews.count > 0;
  Serial.printf("News received: %u headlines\n", latestNews.count);
  return latestNews.valid;
}

void loadInitialData() {
  showStatus("GETTING STOCK QUOTE", "REQUESTING STOCKDATA.ORG JSON", COLOR_CYAN);
  if (!fetchQuote()) {
    showStatus("STOCK REQUEST FAILED", "CHECK SERIAL MONITOR FOR DETAILS", COLOR_RED);
    return;
  }

  showStatus("BUILDING MARKET VIEW", "LOADING CHARTS, VOLUME, AND NEWS", COLOR_CYAN);
  fetchEodHistory();
  lastChartAttempt = millis();
  fetchIntradayChart(RANGE_1D);
  fetchIntradayChart(RANGE_5D);
  fetchNews();

  if (!qrModalVisible) {
    drawStockScreen();
  }
}

bool pointInside(
  int16_t pointX,
  int16_t pointY,
  int16_t x,
  int16_t y,
  int16_t width,
  int16_t height
) {
  return pointX >= x && pointX < x + width &&
    pointY >= y && pointY < y + height;
}

TouchReadState readLandscapeTouch(
  int16_t& screenX,
  int16_t& screenY,
  uint8_t& eventFlag
) {
  eventFlag = TouchDrvFT6X36::EVENT_NONE;
  if (!touchAvailable) {
    return TOUCH_READ_ERROR;
  }

  constexpr uint8_t TOUCH_REGISTER_BYTES = 5;
  Wire.beginTransmission(FT6X36_SLAVE_ADDRESS);
  Wire.write(FT6X36_REG_STATUS);
  if (Wire.endTransmission(false) != 0) {
    return TOUCH_READ_ERROR;
  }

  if (
    Wire.requestFrom(FT6X36_SLAVE_ADDRESS, TOUCH_REGISTER_BYTES) !=
    TOUCH_REGISTER_BYTES
  ) {
    while (Wire.available()) {
      Wire.read();
    }
    return TOUCH_READ_ERROR;
  }

  uint8_t pointCount = Wire.read() & 0x0F;
  uint8_t touchXHigh = Wire.read();
  uint8_t touchXLow = Wire.read();
  uint8_t touchYHigh = Wire.read();
  uint8_t touchYLow = Wire.read();
  eventFlag = (touchXHigh >> 6) & 0x03;

  if (
    pointCount == 0 ||
    pointCount == 0x0F ||
    eventFlag == TouchDrvFT6X36::EVENT_PUT_UP ||
    eventFlag == TouchDrvFT6X36::EVENT_NONE
  ) {
    return TOUCH_RELEASED;
  }

  int16_t rawX = ((touchXHigh & 0x0F) << 8) | touchXLow;
  int16_t rawY = ((touchYHigh & 0x0F) << 8) | touchYLow;

  screenX = rawY;
  screenY = (LCD_HOR_RES - 1) - rawX;

  if (
    screenX < 0 || screenX >= SCREEN_WIDTH ||
    screenY < 0 || screenY >= SCREEN_HEIGHT
  ) {
    return TOUCH_READ_ERROR;
  }

  return TOUCH_PRESSED;
}

void selectChartRange(ChartRange range) {
  unsigned long startedAt = millis();
  selectedRange = range;

  ChartSeries* target = nullptr;
  if (range == RANGE_1D) target = &chart1D;
  if (range == RANGE_5D) target = &chart5D;
  if (range == RANGE_1M) target = &chart1M;

  const char* rangeName = "1D";
  if (range == RANGE_5D) rangeName = "5D";
  if (range == RANGE_1M) rangeName = "1M";

  if (target != nullptr && target->valid) {
    drawChart();
    Serial.printf(
      "Chart range %s displayed from cache in %lu ms\n",
      rangeName,
      millis() - startedAt
    );
    return;
  }

  // Show the selected button immediately before a slow recovery request.
  drawChart();

  unsigned long lastAttempt =
    lastRangeFetchAttempt[static_cast<uint8_t>(range)];
  bool retryAllowed = lastAttempt == 0 ||
    millis() - lastAttempt >= CHART_RETRY_INTERVAL_MS;

  if (
    target != nullptr &&
    !target->valid &&
    retryAllowed &&
    WiFi.status() == WL_CONNECTED
  ) {
    fetchIntradayChart(range);
  } else if (target != nullptr && !target->valid && !retryAllowed) {
    Serial.printf(
      "Chart range %s retry deferred to keep touch responsive\n",
      rangeName
    );
  }

  if (!qrModalVisible) {
    drawChart();
  }

  Serial.printf(
    "Chart range %s recovery completed in %lu ms\n",
    rangeName,
    millis() - startedAt
  );
}

void pollTouch() {
  int16_t screenX = 0;
  int16_t screenY = 0;
  uint8_t eventFlag = TouchDrvFT6X36::EVENT_NONE;
  TouchReadState touchState = readLandscapeTouch(
    screenX,
    screenY,
    eventFlag
  );

  if (touchState == TOUCH_READ_ERROR) {
    return;
  }

  if (touchState == TOUCH_RELEASED) {
    if (touchWasDown && millis() - lastTouchActionAt >= 500) {
      Serial.printf(
        "Touch latch released after %lu ms\n",
        millis() - lastTouchActionAt
      );
    }
    touchWasDown = false;
    return;
  }

  bool recoveredNewPress =
    touchWasDown &&
    eventFlag == TouchDrvFT6X36::EVENT_PUT_DOWN &&
    millis() - lastTouchActionAt >= TOUCH_NEW_PRESS_RECOVERY_MS;

  if (touchWasDown && !recoveredNewPress) {
    return;
  }

  touchWasDown = true;
  lastTouchActionAt = millis();
  Serial.printf(
    "Touch X:%d Y:%d event:%u\n",
    screenX,
    screenY,
    eventFlag
  );

  if (qrModalVisible) {
    qrModalVisible = false;
    drawStockScreen();
    Serial.println("Channel QR code dismissed");
    return;
  }

  if (screenX < LOGO_TOUCH_WIDTH && screenY < LOGO_TOUCH_HEIGHT) {
    qrModalVisible = true;
    drawChannelQrModal();
    Serial.println("Channel QR code displayed");
    return;
  }

  if (pointInside(
    screenX,
    screenY,
    RANGE_TOUCH_1D_X,
    RANGE_TOUCH_Y,
    RANGE_TOUCH_1D_WIDTH,
    RANGE_TOUCH_HEIGHT
  )) {
    selectChartRange(RANGE_1D);
  } else if (pointInside(
    screenX,
    screenY,
    RANGE_TOUCH_5D_X,
    RANGE_TOUCH_Y,
    RANGE_TOUCH_5D_WIDTH,
    RANGE_TOUCH_HEIGHT
  )) {
    selectChartRange(RANGE_5D);
  } else if (pointInside(
    screenX,
    screenY,
    RANGE_TOUCH_1M_X,
    RANGE_TOUCH_Y,
    RANGE_TOUCH_1M_WIDTH,
    RANGE_TOUCH_HEIGHT
  )) {
    selectChartRange(RANGE_1M);
  }
}

uint32_t calculateScreenshotCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;

  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }

  return ~crc;
}

void writeScreenshotBytes(const uint8_t* data, size_t length) {
  size_t offset = 0;

  while (offset < length) {
    size_t bytesRemaining = length - offset;
    size_t chunkSize = min(bytesRemaining, SCREENSHOT_SERIAL_CHUNK_SIZE);
    size_t bytesWritten = Serial.write(data + offset, chunkSize);

    if (bytesWritten == 0) {
      delay(1);
      continue;
    }

    offset += bytesWritten;
    yield();
  }
}

void streamScreenshot() {
  if (!latestQuote.valid) {
    Serial.println("MPD_SCREENSHOT_ERROR NO_STOCK_DATA");
    return;
  }

  Serial.println("Screenshot request received");
  Serial.printf("Free PSRAM before capture: %u bytes\n", ESP.getFreePsram());

  Arduino_Canvas* screenshotCanvas = new Arduino_Canvas(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    nullptr
  );

  if (
    screenshotCanvas == nullptr ||
    !screenshotCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)
  ) {
    delete screenshotCanvas;
    Serial.println("MPD_SCREENSHOT_ERROR FRAMEBUFFER_ALLOCATION_FAILED");
    return;
  }

  Arduino_GFX* normalDisplayTarget = gfx;
  gfx = screenshotCanvas;
  if (qrModalVisible) {
    drawChannelQrModal();
  } else {
    drawStockScreen();
  }
  gfx = normalDisplayTarget;

  uint8_t* framebufferBytes = reinterpret_cast<uint8_t*>(
    screenshotCanvas->getFramebuffer()
  );
  uint32_t checksum = calculateScreenshotCrc32(
    framebufferBytes,
    SCREENSHOT_BYTE_COUNT
  );

  Serial.flush();
  Serial.println("MPD_SCREENSHOT_BEGIN 1");
  Serial.printf("WIDTH %u\n", SCREEN_WIDTH);
  Serial.printf("HEIGHT %u\n", SCREEN_HEIGHT);
  Serial.println("FORMAT RGB565_LE");
  Serial.printf("LENGTH %u\n", static_cast<unsigned int>(SCREENSHOT_BYTE_COUNT));
  Serial.printf("CRC32 %08lX\n", static_cast<unsigned long>(checksum));
  Serial.println("DATA");
  Serial.flush();

  writeScreenshotBytes(framebufferBytes, SCREENSHOT_BYTE_COUNT);
  Serial.print("\nMPD_SCREENSHOT_END\n");
  Serial.flush();

  delete screenshotCanvas;

  if (!qrModalVisible) {
    lastHeaderClock[0] = '\0';
    lastHeaderClockRefresh = 0;
    updateHeaderClock(true);
  }

  Serial.printf("Free PSRAM after capture: %u bytes\n", ESP.getFreePsram());
  Serial.println("Screenshot transfer completed");
}

void handleSerialCommand(const char* command) {
  if (strcmp(command, SCREENSHOT_COMMAND) == 0) {
    streamScreenshot();
  } else if (command[0] != '\0') {
    Serial.printf("Unknown command: %s\n", command);
  }
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    char character = static_cast<char>(Serial.read());

    if (character == '\r') {
      continue;
    }

    if (character == '\n') {
      serialCommandBuffer[serialCommandLength] = '\0';
      handleSerialCommand(serialCommandBuffer);
      serialCommandLength = 0;
      serialCommandBuffer[0] = '\0';
      continue;
    }

    if (character >= 0x20 && character <= 0x7E) {
      if (serialCommandLength < sizeof(serialCommandBuffer) - 1) {
        serialCommandBuffer[serialCommandLength++] = character;
      } else {
        serialCommandLength = 0;
        serialCommandBuffer[0] = '\0';
        Serial.println("Serial command was too long and was discarded");
      }
    }
  }
}

void refreshDataIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastConnectionAttempt >= WIFI_RETRY_INTERVAL_MS) {
      if (connectToWiFi() && !latestQuote.valid) {
        loadInitialData();
      }
    }
    return;
  }

  bool screenNeedsRedraw = false;

  if (millis() - lastQuoteAttempt >= QUOTE_REFRESH_INTERVAL_MS) {
    screenNeedsRedraw |= fetchQuote();
  }

  if (millis() - lastEodAttempt >= EOD_REFRESH_INTERVAL_MS) {
    screenNeedsRedraw |= fetchEodHistory();
  }

  if (millis() - lastNewsAttempt >= NEWS_REFRESH_INTERVAL_MS) {
    screenNeedsRedraw |= fetchNews();
  }

  if (
    selectedRange == RANGE_1D &&
    millis() - lastChartAttempt >= CHART_REFRESH_INTERVAL_MS
  ) {
    lastChartAttempt = millis();
    screenNeedsRedraw |= fetchIntradayChart(RANGE_1D);
  }

  if (screenNeedsRedraw && !qrModalVisible) {
    drawStockScreen();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("MPD Stocks starting");

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

  if (!display->begin(40000000, SPI_MODE0)) {
    stopWithError("ERROR: Display initialization failed");
  }

  digitalWrite(GFX_BL, HIGH);

  touchAvailable = touch.begin(Wire, FT6X36_SLAVE_ADDRESS);
  if (touchAvailable) {
    Serial.println("FT6336 touch initialized");
  } else {
    Serial.println("WARNING: FT6336 touch controller not detected");
  }

  if (connectToWiFi()) {
    loadInitialData();
  }
}

void loop() {
  pollSerialCommands();
  pollTouch();
  refreshDataIfNeeded();

  if (!qrModalVisible) {
    updateHeaderClock();
  }

  delay(5);
}
