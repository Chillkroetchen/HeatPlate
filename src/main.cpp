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

#define TOUCH_INTERRUPT 36
#define TOUCH_DATA 19
#define TOUCH_CS 17
#define TOUCH_CLK 18

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#define BACKGROUND_COLOR 0x0820
#define TEXT_COLOR 0xFFFF
#define GRAPH_COLOR 0xF800

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
const int BUTTON_PINS[4] = {BUTTON1, BUTTON2, BUTTON3, BUTTON4};
int buttonState[4] = {0, 0, 0, 0};
int lastButtonState[4] = {0, 0, 0, 0};
int buttonPressed[4] = {0, 0, 0, 0};
unsigned long lastDebounce;

#define TFT_DELAY 100
unsigned long lastTFTwrite;

#define MS_TO_S 1000    // ms in s conversion factor
#define US_TO_S 1000000 // us in s conversion factor

// Menu definitions
enum State
{
  STATE_START,
  STATE_PROFILE_SELECTION,
  STATE_REFLOW_LANDING,
  STATE_REFLOW_STARTED,
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

void reflowLandingScreen(const int profileId);
void reflowStartedScreen(const int profileId);
void readButtons()
{
  for (int i = 0; i < 4; i++)
  { // Button Reading Routine
    int reading = digitalRead(BUTTON_PINS[i]);
    if (reading != lastButtonState[i])
    {
      lastDebounce = millis();
    }
    if ((millis() - lastDebounce) > DEBOUNCE_DELAY)
    {
      if (reading != buttonState[i])
      {
        buttonState[i] = reading;

        if (buttonState[i] == HIGH)
        { // Write Button Event here
          buttonPressed[i] = 1;
        }
      }
    }
    lastButtonState[i] = reading;
    // End Button Reading Routine
  }
}

inline int getTotalTime(const int profileId)
{
  float sum_time = 0;
  for (int i = 0; i < 5; i++)
  {
    sum_time += SOLDER_PROFILES[profileId][i][1];
  }
  return sum_time;
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

inline void printStatusChart()
{
  tft.setTextSize(1);
  tft.fillRect(5, 148, 150, 87, TEXT_COLOR);

  const int COLUMNS = 5;
  const int Y_VALUES[COLUMNS] = {150, 167, 184, 201, 218};
  const char *LABEL[COLUMNS] = {"Temp MCU:", "Temp Housing:", "Temp Setpoint:", "Temp Plate:", "Runtime:"};

  for (int row = 0; row < COLUMNS; row++)
  {
    tft.fillRect(7, Y_VALUES[row], 100, 15, BACKGROUND_COLOR);
    tft.fillRect(109, Y_VALUES[row], 44, 15, BACKGROUND_COLOR);
    tft.setCursor(10, Y_VALUES[row] + 4);
    tft.println(LABEL[row]);
  }
}

inline void printStatusChartValues(const int profileId, const int currentTime)
{
  tft.setTextSize(1);

  const int COLUMNS = 5;
  const int Y_VALUES[COLUMNS] = {150, 167, 184, 201, 218};

  for (int row = 0; row < COLUMNS; row++)
  {
    tft.fillRect(109, 150 + ((15 + 2) * row), 44, 15, BACKGROUND_COLOR);
    tft.setCursor(109 + 3, 150 + ((15 + 2) * row) + 4);

    switch (row)
    {
    case 0:
      tft.printf("%d C", int(TEMP1.readCelsius()));
      break;
    case 1:
      tft.printf("%d C", int(TEMP2.readCelsius()));
      break;
    case 2:
      tft.printf("%d C", int(SOLDER_PROFILES[profileId][2][0]));
      break;
    case 3:
      tft.printf("%d C", int(TEMP3.readCelsius()));
      break;
    case 4:
      tft.print(getTotalTime(profileId) - currentTime);
      tft.println(" s");
      break;
    }
  }
}

inline void printTemperatureGraph(const int profileId)
{
  const int BOTTOM_LEFT_X = 12;
  const int BOTTOM_LEFT_Y = 132;
  const int TOP_RIGHT_X = 310;
  const int TOP_RIGHT_Y = 10;
  const int WIDTH = TOP_RIGHT_X - BOTTOM_LEFT_X;
  const int HEIGHT = BOTTOM_LEFT_Y - TOP_RIGHT_Y;

  int x = BOTTOM_LEFT_X;
  int y = BOTTOM_LEFT_Y;

  // calculate X/Y coordinates of reflow curve in respect to screen space available
  for (int i = 0; i < 5; i++)
  {
    int deltaX = (SOLDER_PROFILES[profileId][i][1] / (float)getTotalTime(profileId)) * WIDTH;
    int deltaY = (SOLDER_PROFILES[profileId][i][0] / (float)SOLDER_PROFILES[profileId][2][0]) * HEIGHT;

    tft.drawLine(x, y, x + deltaX, BOTTOM_LEFT_Y - deltaY, TEXT_COLOR);

    y = BOTTOM_LEFT_Y - deltaY;
    x += deltaX;
  }

  tft.fillRect(5, 5, 2, 134, TEXT_COLOR);   // y-axis
  tft.fillRect(5, 137, 310, 2, TEXT_COLOR); // x-axis
}

inline void printReflowGraph(const int profileId, const int currentTime)
{
  const int BOTTOM_LEFT_X = 12;
  const int BOTTOM_LEFT_Y = 132;
  const int TOP_RIGHT_X = 310;
  const int TOP_RIGHT_Y = 10;
  const int WIDTH = TOP_RIGHT_X - BOTTOM_LEFT_X;
  const int HEIGHT = BOTTOM_LEFT_Y - TOP_RIGHT_Y;

  float x = BOTTOM_LEFT_X + ((currentTime / (float)getTotalTime(profileId)) * WIDTH);
  float y = BOTTOM_LEFT_Y - (TEMP3.readCelsius() / (float)SOLDER_PROFILES[profileId][2][0]) * HEIGHT;

  tft.drawPixel(x, y, GRAPH_COLOR);
}

void startScreen(const int profileId)
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(BACKGROUND_COLOR);

  tft.fillRect(100, 50, 120, 40, TEXT_COLOR);
  tft.setCursor(107, 57);
  tft.println("Selected Profile:");
  tft.setCursor(107, 75);
  tft.setTextColor(GRAPH_COLOR);
  tft.println(PROFILE_NAMES[profileId]);
  tft.setTextColor(BACKGROUND_COLOR);

  printStartScreenOption(0, "Start Reflow");
  printStartScreenOption(1, "Select Profile");
  printStartScreenOption(2, "Placeholder");
  printStartScreenOption(3, "Placeholder");
}

void profileSelectScreen()
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(BACKGROUND_COLOR);

  tft.fillRect(80, 60, 160, 20, TEXT_COLOR);
  tft.setCursor(88, 67);
  tft.println("Select Profile:");

  // for (int i = 0; i < Profile::MAX; i++) use this line to include custom profiles in screen
  for (int i = 0; i < 4; i++)
  {
    tft.fillRect(80, 85 + 25 * i, 20, 20, TEXT_COLOR);
    tft.setCursor(88, 92 + 25 * i);
    tft.println(i + 1);
    tft.fillRect(105, 85 + 25 * i, 135, 20, TEXT_COLOR);
    tft.setCursor(110, 92 + 25 * i);
    tft.println(PROFILE_NAMES[i]);
  }
}

void reflowLandingScreen(const int profileId)
{
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);

  printTemperatureChart(profileId);
  printStatusChart();

  // this does not need to be done every time.
  // consider caching the result
  printTemperatureGraph(profileId);

  // textbox for abort and start
  tft.fillRect(15, 10, 100, 30, TEXT_COLOR);
  tft.setTextColor(BACKGROUND_COLOR);
  tft.setCursor(17, 14);
  tft.println("Press 1 to abort");
  tft.setCursor(17, 29);
  tft.println("Press 2 to start");
  tft.setTextColor(TEXT_COLOR);

  // print max temp and total time of selected profile
  tft.setCursor(100, 90);
  tft.setTextColor(GRAPH_COLOR);
  tft.printf("Profile: %s\n", PROFILE_NAMES[profileId]);
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(100, 105);
  tft.printf("Max temp: %d C\n", (int)SOLDER_PROFILES[profileId][2][0]);
  tft.setCursor(100, 120);
  tft.printf("Total time: %d s\n", getTotalTime(profileId));

  // endless loop to keep screen updated
  while (true)
  {
    readButtons();
    // Serial.printf("Buttons: %d %d %d %d\n", buttonPressed[0], buttonPressed[1], buttonPressed[2], buttonPressed[3]);
    if (buttonPressed[0] == 1)
    {
      buttonPressed[0] = 0;
      currentState = STATE_START;
      startScreen(currentProfile);
      break;
    }
    else if (buttonPressed[1] == 1)
    {
      buttonPressed[1] = 0;
      currentState = STATE_REFLOW_STARTED;
      reflowStartedScreen(profileId);
      break;
    }

    if (millis() >= lastTFTwrite + 1000)
    {
      printStatusChartValues(profileId, 0);
      lastTFTwrite = millis();
    }
  }
}

void reflowStartedScreen(const int profileId)
{
  // textbox for abort
  tft.fillRect(15, 25, 100, 15, BACKGROUND_COLOR);
  tft.fillRect(15, 10, 137, 15, TEXT_COLOR);
  tft.setTextColor(BACKGROUND_COLOR);
  tft.setCursor(17, 14);
  tft.println("Press any key to abort");
  tft.setTextColor(TEXT_COLOR);

  lastTFTwrite = millis();
  int t = 0;
  int tmax = getTotalTime(profileId);
  // endless loop to keep screen updated
  while (true)
  {
    bool breakLoop = false;
    readButtons();
    for (int i = 0; i < 4; i++)
    {
      if (buttonPressed[i] == 1)
      {
        breakLoop = true;
        break;
      }
    }
    if (breakLoop == true || t >= tmax)
    {
      for (int i = 0; i < 4; i++)
      {
        buttonPressed[i] = 0;
      }
      currentState = STATE_REFLOW_LANDING;
      reflowLandingScreen(profileId);
      break;
    }

    if (millis() >= lastTFTwrite + 1000)
    {
      printReflowGraph(profileId, t);
      printStatusChartValues(profileId, t);
      lastTFTwrite = millis();
      t++;
    }
  }
}

void updateReflowValues()
{
}

void setup(void)
{
  Serial.begin(115200);

  pinMode(BUTTON1, INPUT);
  pinMode(BUTTON2, INPUT);
  pinMode(BUTTON3, INPUT);
  pinMode(BUTTON4, INPUT);
  Serial.println("Temp Sensor test");
  tft.init(240, 320); // Init ST7789 320x240
  Serial.println("TFT Initialized");
  tft.invertDisplay(false);
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(45);

  startScreen(currentProfile);

  Serial.println("Temperature Readings:");
  Serial.printf("\tSensor 1: %f °C", TEMP1.readCelsius());
  Serial.printf("\tSensor 2: %f °C", TEMP2.readCelsius());
  Serial.printf("\tSensor 3: %f °C", TEMP3.readCelsius());
}

void loop()
{
  readButtons();

  if (millis() < lastTFTwrite + TFT_DELAY)
  {
    return;
  }

  // Serial.printf("Buttons: %d %d %d %d\n", buttonPressed[0], buttonPressed[1], buttonPressed[2], buttonPressed[3]);

  switch (currentState)
  {
  case STATE_START:
    if (buttonPressed[0] == 1)
    {
      buttonPressed[0] = 0;
      currentState = STATE_REFLOW_LANDING;
      reflowLandingScreen(currentProfile);
    }
    else if (buttonPressed[1] == 1)
    {
      buttonPressed[1] = 0;
      currentState = STATE_PROFILE_SELECTION;
      profileSelectScreen();
    }
    break;
  case STATE_REFLOW_LANDING:
    buttonPressed[0] = 0;
    currentState = STATE_START;
    startScreen(currentProfile);
    break;
  case STATE_PROFILE_SELECTION:
    if (buttonPressed[0] == 1)
    {
      buttonPressed[0] = 0;
      currentProfile = PROFILE_STANDARD_UNLEADED;
    }
    else if (buttonPressed[1] == 1)
    {
      buttonPressed[1] = 0;
      currentProfile = PROFILE_FAST_UNLEADED;
    }
    else if (buttonPressed[2] == 1)
    {
      buttonPressed[2] = 0;
      currentProfile = PROFILE_STANDARD_LEADED;
    }
    else if (buttonPressed[3] == 1)
    {
      Serial.println(buttonPressed[3]);
      buttonPressed[3] = 0;
      currentProfile = PROFILE_FAST_LEADED;
    }
    else
    {
      break;
    }

    currentState = STATE_START;
    startScreen(currentProfile);
    break;
  default:
    break;
  }

  for (int i = 0; i < 4; i++)
  {
    buttonPressed[i] = 0;
  }

  lastTFTwrite = millis();
}
