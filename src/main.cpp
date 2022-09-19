#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <Arduino.h>
#include <SPI.h>
#include <max6675.h>

#define TFT_CS 5
#define TFT_RST 2
#define TFT_DC 0
#define TFT_MOSI 23 // Data out
#define TFT_SCLK 18 // Clock out

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#define BACKGROUND_COLOR 0x0820
#define TEXT_COLOR 0xFFFF

#define TEMP_SO 26
#define TEMP_CS1 33
#define TEMP_CS2 14
#define TEMP_CS3 27
#define TEMP_SCK 25

MAX6675 TEMP1(TEMP_SCK, TEMP_CS1, TEMP_SO);
MAX6675 TEMP2(TEMP_SCK, TEMP_CS2, TEMP_SO);
MAX6675 TEMP3(TEMP_SCK, TEMP_CS3, TEMP_SO);

// Button definitions
#define BUTTON1 32
#define BUTTON2 35
#define BUTTON3 34
#define BUTTON4 39
#define DEBOUNCE_DELAY 50
const int BUTTON_PINS[4] = {BUTTON1, BUTTON2, BUTTON3, BUTTON3};
int buttonState[4] = {0, 0, 0, 0};
int lastButtonState[4] = {0, 0, 0, 0};
int buttonPressed[4] = {0, 0, 0, 0};
unsigned long lastDebounce;

#define TFT_DELAY 1
unsigned long lastTFTwrite;

#define MS_TO_S 1000    // ms in s conversion factor
#define US_TO_S 1000000 // us in s conversion factor

// Menu definitions
enum State
{
  STATE_START,
  STATE_PROFILE_SELECTION,
  STATE_REFLOW
} currentState;

enum Profile
{
  PROFILE_STANDARD_UNLEADED,
  PROFILE_FAST_UNLEADED,
  PROFILE_STANDARD_LEADED,
  PROFILE_FAST_LEADED,
  PROFILE_CUSTOM1,
  PROFILE_CUSTOM2,
  MAX,
} currentProfile;

const char *PROFILE_NAMES[Profile::MAX] = {"Standard Unleaded", "Fast Unleaded", "Standard Leaded",
                                           "Fast Leaded",       "Custom 1",      "Custom 2"};
const int SOLDER_PROFILES[Profile::MAX][5][2]{
    {{170, 85}, {170, 100}, {260, 45}, {260, 25}, {30, 60}}, // Standard Unleaded
    {{150, 30}, {200, 60}, {260, 20}, {260, 20}, {30, 40}},  // Fast Unleaded
    {{150, 75}, {150, 90}, {220, 35}, {220, 35}, {30, 65}},  // Standard Leaded
    {{130, 35}, {180, 30}, {230, 20}, {230, 30}, {30, 50}},  // Fast Leaded
    {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},                // Custom 1
    {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}                 // Custom 2
};

void homeScreen(int profileId)
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);

  printTemperatureChart(profileId);

  // print chart for status
  tft.setTextSize(1);
  tft.fillRect(5, 148, 150, 87, TEXT_COLOR);

  const int statX[2] = {7, 109};
  const int statY[5] = {150, 167, 184, 201, 218};
  const int lWidth = 100;
  const int vWidth = 44;
  const int statHeight = 15;

  for (int x = 0; x < 2; x++)
  {
    for (int y = 0; y < 5; y++)
    {
      int width;
      if (x == 0)
      {
        width = lWidth;
      }
      else
      {
        width = vWidth;
      }
      tft.fillRect(statX[x], statY[y], width, statHeight, BACKGROUND_COLOR);
    }
  }

  const char *statLable[5] = {"Temp MCU:", "Temp Housing:", "Temp Setpoint:", "Temp Plate:", "Runtime:"};
  for (int i = 0; i < 5; i++)
  {
    tft.setCursor(statX[0] + 3, statY[i] + 4);
    tft.println(statLable[i]);
  }

  // print coordinate system
  const int graphCoord[2][2] = {12, 132, 310, 10};
  const float maxTemp = SOLDER_PROFILES[profileId][2][0];
  float sumTime = 0;
  int X = graphCoord[0][0];
  int Y = graphCoord[0][1];

  // calculate X/Y coordinates of reflow curve in respect to screen space available
  for (int n = 0; n < 5; n++)
  {
    sumTime = sumTime + SOLDER_PROFILES[profileId][n][1];
  }
  for (int i = 0; i < 5; i++)
  {
    int deltaX = (SOLDER_PROFILES[profileId][i][1] / sumTime) * (graphCoord[1][0] - graphCoord[0][0]);
    int deltaY = (SOLDER_PROFILES[profileId][i][0] / maxTemp) * (graphCoord[0][1] - graphCoord[1][1]);
    tft.drawLine(X, Y, X + deltaX, graphCoord[0][1] - deltaY, TEXT_COLOR);
    Y = graphCoord[0][1] - deltaY;
    X = X + deltaX;
  }
  // end of reflow curve calculation

  tft.fillRect(5, 5, 2, 134, TEXT_COLOR);   // y-axis
  tft.fillRect(5, 137, 310, 2, TEXT_COLOR); // x-axis

  // textbox for abort
  tft.fillRect(15, 10, 100, 30, TEXT_COLOR);
  tft.setTextColor(BACKGROUND_COLOR);
  tft.setCursor(17, 14);
  tft.println("Press 1 to abort");
  tft.setCursor(17, 29);
  tft.println("Press 2 to start");
  tft.setTextColor(TEXT_COLOR);

  // print max temp and total time of selected profile
  tft.setCursor(100, 105);
  tft.print("Max. Temp.: ");
  tft.print(maxTemp, 0);
  tft.print(" ");
  tft.cp437(true);
  tft.write(167);
  tft.println("C");
  tft.setCursor(100, 120);
  tft.print("Total Time: ");
  tft.print(sumTime, 0);
  tft.println(" s");
}

inline void printTemperatureChart(const int profileId)
{
  // print chart for temp curve
  tft.setTextSize(1);

  // print border
  tft.fillRect(201, 144, 114, 91, TEXT_COLOR);

  const int COLUMNS = 3;
  const int ROWS = 6;
  const int WIDTH = 36;
  const char *TITLES[COLUMNS] = {"Point", "Temp", "Time"};
  const int X_VALUES[COLUMNS] = {203, 240, 277};
  const int Y_VALUES[ROWS] = {146, 164, 178, 192, 206, 220};

  char cell_content_buf[3];

  for (int col = 0; col < COLUMNS; col++)
  {
    // print title of column
    tft.fillRect(X_VALUES[col], Y_VALUES[0], WIDTH, 16, BACKGROUND_COLOR);
    tft.setCursor(X_VALUES[col] + 3, Y_VALUES[0] + 4);
    tft.print(TITLES[col]);

    // print colum data
    for (int row = 1; row < ROWS; row++)
    {
      tft.fillRect(X_VALUES[col], Y_VALUES[row], WIDTH, 13, BACKGROUND_COLOR);
      tft.setCursor(X_VALUES[col] + 10, Y_VALUES[row] + 3);

      if (col == 0)
      {
        // print point number
        tft.print(row);
        continue;
      }

      // print temperature or time
      sprintf(cell_content_buf, "%d", SOLDER_PROFILES[profileId][row - 1][col - 1]);
      tft.print(cell_content_buf);
    }
  }
}

void startScreen()
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(BACKGROUND_COLOR);

  tft.fillRect(100, 50, 120, 40, TEXT_COLOR);
  tft.setCursor(107, 57);
  tft.println("Selected Profile:");
  tft.setCursor(107, 75);
  tft.println("Standard Unleaded");

  printStartScreenOption(0, "Start Reflow");
  printStartScreenOption(1, "Select Profile");
  printStartScreenOption(2, "Placeholder");
  printStartScreenOption(3, "Placeholder");
}

inline void printStartScreenOption(const int line, const char *text)
{
  tft.fillRect(100, 95 + 25 * line, 20, 20, TEXT_COLOR);
  tft.setCursor(108, 101 + 25 * line);
  tft.println(line + 1);
  tft.fillRect(125, 95 + 25 * line, 95, 20, TEXT_COLOR);
  tft.setCursor(130, 101 + 25 * line);
  tft.println(text);
}

void readButtons()
{
  for (int i = 0; i < 4; i++)
  {
    const int reading = digitalRead(BUTTON_PINS[i]);

    if (reading != lastButtonState[i])
    {
      lastDebounce = millis();
    }

    lastButtonState[i] = reading;

    if (!((millis() - lastDebounce) > DEBOUNCE_DELAY && reading != buttonState[i]))
    {
      return;
    }

    buttonState[i] = reading;

    if (buttonState[i] == HIGH)
    { // Write Button Event here
      buttonPressed[i] = 1;
    }
  }
}

void setup(void)
{
  Serial.begin(115200);

  // delay(5000);
  pinMode(BUTTON1, INPUT);
  pinMode(BUTTON2, INPUT);
  pinMode(BUTTON3, INPUT);
  pinMode(BUTTON4, INPUT);
  Serial.println(F("Temp Sensor test"));
  tft.init(240, 320); // Init ST7789 320x240
  Serial.println(F("TFT Initialized"));
  tft.invertDisplay(false);
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(45);

  // homeScreen(STANDARD_LEADED);
  // delay(5000);
  startScreen();
  // tft.fillScreen(BACKGROUND_COLOR);

  Serial.println(F("Temperature Readings:"));
  Serial.print(F("Sensor 1: "));
  Serial.print(TEMP1.readCelsius());
  Serial.println(F(" °C"));
  Serial.print(F("Sensor 2: "));
  Serial.print(TEMP2.readCelsius());
  Serial.println(F(" °C"));
  Serial.print(F("Sensor 3: "));
  Serial.print(TEMP3.readCelsius());
  Serial.println(F(" °C"));
}

void loop()
{
  readButtons();

  if (millis() < lastTFTwrite + TFT_DELAY * MS_TO_S)
  {
    return;
  }

  if (buttonPressed[0] == 1 && currentState == STATE_START)
  {
    currentState = STATE_REFLOW;
    currentProfile = PROFILE_STANDARD_LEADED;
    homeScreen(currentProfile);
  }
  else if (buttonPressed[0] == 1 && currentState == STATE_REFLOW)
  {
    currentState = STATE_START;
    startScreen();
  }

  String msg = "Buttons: ";
  for (int i = 0; i < 4; i++)
  {
    msg = msg + String(buttonPressed[i]) + " ";
    buttonPressed[i] = 0;
  }
  Serial.println(msg);

  lastTFTwrite = millis();
}
