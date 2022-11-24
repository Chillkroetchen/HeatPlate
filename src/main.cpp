#include <AceButton.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <Arduino.h>
#include <PID_v1.h>
#include <SPI.h>
#include <max6675.h>

using namespace ace_button;

// PID and SSR Definitions
#define SSR_PIN 25

// TFT and Touch Definitions
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

#define TFT_DELAY 100
unsigned long lastTFTwrite;

// Temp Sensor Definitions
#define TEMP_SO 26
#define TEMP_CS1 33
#define TEMP_CS2 14
#define TEMP_CS3 27
#define TEMP_SCK 12

MAX6675 TEMP1(TEMP_SCK, TEMP_CS1, TEMP_SO);
MAX6675 TEMP2(TEMP_SCK, TEMP_CS2, TEMP_SO);
MAX6675 TEMP3(TEMP_SCK, TEMP_CS3, TEMP_SO);

// Button definitions
#define BUTTON_PIN1 32
#define BUTTON_PIN2 35
#define BUTTON_PIN3 34
#define BUTTON_PIN4 39
#define DEBOUNCE_DELAY 50
const int BUTTON_PINS[4] = {BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3, BUTTON_PIN4};

AceButton button1(BUTTON_PIN1);
AceButton button2(BUTTON_PIN2);
AceButton button3(BUTTON_PIN3);
AceButton button4(BUTTON_PIN4);

#define MS_TO_S 1000    // ms in s conversion factor
#define US_TO_S 1000000 // us in s conversion factor

// Menu definitions
// current state of device
enum State
{
  STATE_START,
  STATE_PROFILE_SELECTION,
  STATE_REFLOW_LANDING,
  STATE_REFLOW_STARTED,
  STATE_REFLOW_FINISHED,
} currentState;

// currently set reflow profile
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

// names of hardcoded reflow profiles
const char *PROFILE_NAMES[Profile::MAX] = {"Standard Unleaded", "Fast Unleaded", "Standard Leaded",
                                           "Fast Leaded",       "Custom 1",      "Custom 2"};
// hardcoded reflow profiles consisting of {temp, time}
const int SOLDER_PROFILES[Profile::MAX][5][2]{
    {{170, 85}, {170, 100}, {260, 45}, {260, 25}, {30, 60}}, // Standard Unleaded
    {{150, 30}, {200, 60}, {260, 20}, {260, 20}, {30, 40}},  // Fast Unleaded
    {{150, 75}, {150, 90}, {220, 35}, {220, 35}, {30, 65}},  // Standard Leaded
    {{130, 35}, {180, 30}, {230, 20}, {230, 30}, {30, 50}},  // Fast Leaded
    {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},                // Custom 1
    {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}                 // Custom 2
};

int reflowRuntime = 0;

/* Prototypes start */
void reflowLandingScreen(const int profileId);
void reflowStartedScreen(const int profileId);
void handleEvent(AceButton *, uint8_t, uint8_t);
/* Prototypes end */

// calculate total time of selected solder profile
inline int getTotalTime(const int profileId)
{
  int sum_time = 0;
  for (int i = 0; i < 5; i++)
  {
    sum_time += SOLDER_PROFILES[profileId][i][1];
  }
  return sum_time;
}

// fill one line in start screen with identifier and text
inline void printStartScreenOption(const int line, const char *text)
{
  Serial.println("TRACE > printStartScreenOption()");
  tft.fillRect(100, 95 + 25 * line, 20, 20, TEXT_COLOR);
  tft.setCursor(108, 101 + 25 * line);
  tft.println(line + 1);
  tft.fillRect(125, 95 + 25 * line, 95, 20, TEXT_COLOR);
  tft.setCursor(130, 101 + 25 * line);
  tft.println(text);
}

// print temperature chart in reflow screen and fill with values of currently selected reflow profile
inline void printTemperatureChart(const int profileId)
{
  Serial.println("TRACE > printTemperatureChart()");

  // print chart for temp curve
  tft.setTextSize(1);

  // print border
  tft.fillRect(201, 144, 114, 91, TEXT_COLOR);

  const int COLUMNS = 3;
  const int ROWS = 6;
  const int WIDTH = 36;
  const char *TITLES[COLUMNS] = {"Point", "Temp", "Time"};
  const int X_VALUES[COLUMNS] = {203, 240, 277};             // delta = 37 each
  const int Y_VALUES[ROWS] = {146, 164, 178, 192, 206, 220}; // delta = 18 first line, delta = 14 other lines

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

// print status chart in reflow screen
inline void printStatusChart()
{
  Serial.println("TRACE > printStatusChart()");

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

// fill/update status chart with values in reflow screen
inline void printStatusChartValues(const int profileId, const int currentTime)
{
  Serial.println("TRACE > printStatusChartValues()");

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
      if (int(TEMP1.readCelsius()) > 50)
      {
        tft.setTextColor(GRAPH_COLOR);
        tft.printf("WARN!");
        tft.setTextColor(TEXT_COLOR);
      }
      else
      {
        tft.printf("%d C", int(TEMP1.readCelsius()));
      }
      break;
    case 1:
      if (int(TEMP2.readCelsius()) > 50)
      {
        tft.setTextColor(GRAPH_COLOR);
        tft.printf("WARN!");
        tft.setTextColor(TEXT_COLOR);
      }
      else
      {
        tft.printf("%d C", int(TEMP2.readCelsius()));
      }
      break;
    case 2:
      tft.printf("%d C", int(SOLDER_PROFILES[profileId][2][0]));
      break;
    case 3:
      if (int(TEMP3.readCelsius()) > 280)
      {
        tft.setTextColor(GRAPH_COLOR);
        tft.printf("WARN!");
        tft.setTextColor(TEXT_COLOR);
      }
      else
      {
        tft.printf("%d C", int(TEMP3.readCelsius()));
      }
      break;
    case 4:
      tft.print(getTotalTime(profileId) - currentTime);
      tft.println(" s");
      break;
    }
  }
}

// print ideal temperature graph of selected reflow profile in reflow screen
inline void printTemperatureGraph(const int profileId)
{
  Serial.println("TRACE > printTemperatureGraph()");

  const int BOTTOM_LEFT_X = 12;
  const int BOTTOM_LEFT_Y = 132;
  const int TOP_RIGHT_X = 310;
  const int TOP_RIGHT_Y = 10;
  const int WIDTH = TOP_RIGHT_X - BOTTOM_LEFT_X;
  const int HEIGHT = BOTTOM_LEFT_Y - TOP_RIGHT_Y;

  // initialize x & y with the bottom left corner of screen
  int x = BOTTOM_LEFT_X;
  int y = BOTTOM_LEFT_Y;

  // calculate X/Y coordinates of reflow curve in respect to screen space available
  for (int i = 0; i < 5; i++)
  {
    // calculate required X delta: (StepTime of profile / TotalTime of profile) * ScreenWidth available
    int deltaX = (SOLDER_PROFILES[profileId][i][1] / (float)getTotalTime(profileId)) * WIDTH;
    // calculate required Y delta: (StepTemp of profile / MaxTemp of profile) * ScreenHeight available
    int deltaY = (SOLDER_PROFILES[profileId][i][0] / (float)SOLDER_PROFILES[profileId][2][0]) * HEIGHT;

    // draw line from old X & Y to new X & Y
    tft.drawLine(x, y, x + deltaX, BOTTOM_LEFT_Y - deltaY, TEXT_COLOR);

    // update X & Y with calculated delta to print next line
    y = BOTTOM_LEFT_Y - deltaY;
    x += deltaX;
  }

  tft.fillRect(5, 5, 2, 134, TEXT_COLOR);   // y-axis
  tft.fillRect(5, 137, 310, 2, TEXT_COLOR); // x-axis
}

// print actual temperature graph while reflow process is running
inline void printReflowGraph(const int profileId, const int currentTime)
{
  Serial.println("TRACE > printReflowGraph()");

  const int BOTTOM_LEFT_X = 12;
  const int BOTTOM_LEFT_Y = 132;
  const int TOP_RIGHT_X = 310;
  const int TOP_RIGHT_Y = 10;
  const int WIDTH = TOP_RIGHT_X - BOTTOM_LEFT_X;
  const int HEIGHT = BOTTOM_LEFT_Y - TOP_RIGHT_Y;

  // calculate X: (TimeElapsed of process / TotalTime of profile) * ScreenWidth available
  float x = BOTTOM_LEFT_X + ((currentTime / (float)getTotalTime(profileId)) * WIDTH);
  // calculate Y: (TempSensor / MaxTemp of profile) * ScreenWidth available
  float y = BOTTOM_LEFT_Y - (TEMP3.readCelsius() / (float)SOLDER_PROFILES[profileId][2][0]) * HEIGHT;
  // draw pixel in calculated location
  tft.drawPixel(x, y, GRAPH_COLOR);
}

// print start screen with selected reflow profile
void startScreen(const int profileId)
{
  Serial.println("TRACE > startScreen()");

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

// print profile select screen to select desired reflow profile
void profileSelectScreen()
{
  Serial.println("TRACE > profileSelectScreen()");

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

// print landing screen for reflow process
// user can check all values of selected profile on screen and decide to start or abort
void reflowLandingScreen(const int profileId)
{
  Serial.println("TRACE > reflowLandingScreen()");

  tft.fillScreen(BACKGROUND_COLOR);
  tft.setTextColor(TEXT_COLOR);

  printTemperatureChart(profileId);
  printStatusChart();
  printStatusChartValues(profileId, 0);

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
}

// keep screen updated after reflow process started
void reflowStartedScreen(const int profileId)
{
  Serial.println("TRACE > reflowStartedScreen()");

  // textbox for abort
  tft.fillRect(15, 25, 100, 15, BACKGROUND_COLOR);
  tft.fillRect(15, 10, 137, 15, TEXT_COLOR);
  tft.setTextColor(BACKGROUND_COLOR);
  tft.setCursor(17, 14);
  tft.println("Press any key to abort");
  tft.setTextColor(TEXT_COLOR);

  lastTFTwrite = millis();
}

void reflowFinishedScreen()
{
  tft.fillRect(15, 10, 140, 30, BACKGROUND_COLOR);
  tft.fillRect(100, 90, 130, 40, BACKGROUND_COLOR);
  tft.setCursor(100, 90);
  tft.print("Reflow Done!");
  tft.setCursor(100, 105);
  tft.print("Press any button to continue!");
}

void setup(void)
{
  Serial.begin(115200);

  // Configure the ButtonConfig with the event handler, and enable all higher
  // level events.
  ButtonConfig *buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  // buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  // buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  // buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  // buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);

  // if (button1.isPressedRaw())
  // {
  //   Serial.println(F("setup(): button 1 was pressed while booting"));
  // }

  pinMode(BUTTON_PIN1, INPUT);
  pinMode(BUTTON_PIN2, INPUT);
  pinMode(BUTTON_PIN3, INPUT);
  pinMode(BUTTON_PIN4, INPUT);

  Serial.println("Temp Sensor test");
  tft.init(240, 320); // Init ST7789 320x240
  Serial.println("TFT Initialized");
  tft.invertDisplay(false);
  tft.setRotation(45);

  currentProfile = PROFILE_FAST_LEADED;
  currentState = STATE_START;
  // startScreen(currentProfile);

  Serial.println("Temperature Readings:");
  Serial.printf("\tSensor 1: %f °C", TEMP1.readCelsius());
  Serial.printf("\tSensor 2: %f °C", TEMP2.readCelsius());
  Serial.printf("\tSensor 3: %f °C\n", TEMP3.readCelsius());
}

bool requestedRedraw = true;

void drawScreen()
{
  if (!requestedRedraw)
  {
    return;
  }

  switch (currentState)
  {
  case STATE_START:
    startScreen(currentProfile);
    Serial.println("TRACE > drawscreen(): startScreen");
    break;
  case STATE_PROFILE_SELECTION:
    profileSelectScreen();
    Serial.println("TRACE > drawscreen(): profileSelectScreen");
    break;
  case STATE_REFLOW_LANDING:
    reflowLandingScreen(currentProfile);
    Serial.println("TRACE > drawscreen(): reflowLandingScreen");
    break;
  case STATE_REFLOW_STARTED:
    reflowStartedScreen(currentProfile);
    Serial.println("TRACE > drawscreen(): reflowStartedScreen");
    break;
  case STATE_REFLOW_FINISHED:
    reflowFinishedScreen();
    Serial.println("TRACE > drawscreen(): reflowFinishedScreen");
    break;
  default:
    break;
  }

  requestedRedraw = false;
}

void drawScreenUpdate()
{
  State lastState = currentState;

  if (millis() - lastTFTwrite > 1000)
  {
    lastTFTwrite = millis();

    switch (currentState)
    {
    case STATE_REFLOW_LANDING:
      printStatusChartValues(currentProfile, 0);

      break;
    case STATE_REFLOW_STARTED:
      if (reflowRuntime > getTotalTime(currentProfile))
      {
        reflowRuntime = 0;
        currentState = STATE_REFLOW_FINISHED;
      }
      else
      {
        printReflowGraph(currentProfile, reflowRuntime);
        printStatusChartValues(currentProfile, reflowRuntime);
        reflowRuntime++;
      }

      break;
    default:
      break;
    }
  }

  if (lastState != currentState)
  {
    requestedRedraw = true;
  }
}

void loop()
{
  unsigned long start = millis();

  button1.check();
  button2.check();
  button3.check();
  button4.check();

  drawScreen();
  drawScreenUpdate();

  // Serial.printf("Current Profile: %d\n", currentProfile);

  unsigned long duration = millis() - start;
  if (duration >= 20)
  {
    Serial.printf("WARN > loop(): took too long %d ms\n", duration);
  }
}

void handleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState)
{
  // Print out a message for all events, for both buttons.
  Serial.print(F("TRACE > handleEvent(): pin: "));
  Serial.print(button->getPin());
  Serial.print(F("; eventType: "));
  Serial.print(eventType);
  Serial.print(F("; buttonState: "));
  Serial.print(buttonState);
  Serial.print(F("; currentState: "));
  Serial.print(currentState);
  Serial.print(F("; currentProfile: "));
  Serial.println(currentProfile);

  const int tmax = getTotalTime(currentProfile);

  State lastState = currentState;

  switch (currentState)
  {
  case STATE_START:
    switch (eventType)
    {
    case AceButton::kEventPressed:
      switch (button->getPin())
      {
        // press button 1 to start reflow process
      case BUTTON_PIN1:
        currentState = STATE_REFLOW_LANDING;
        break;
        // press button 2 to select desired reflow profile
      case BUTTON_PIN2:
        currentState = STATE_PROFILE_SELECTION;
        break;
      }
    }

    break;
  case STATE_PROFILE_SELECTION:
    // selects reflow profile according to pressed button
    switch (eventType)
    {
    case AceButton::kEventPressed:
      switch (button->getPin())
      {
      case BUTTON_PIN1:
        currentProfile = PROFILE_STANDARD_UNLEADED;
        break;
      case BUTTON_PIN2:
        currentProfile = PROFILE_FAST_UNLEADED;
        break;
      case BUTTON_PIN3:
        currentProfile = PROFILE_STANDARD_LEADED;
        break;
      case BUTTON_PIN4:
        currentProfile = PROFILE_FAST_LEADED;
        break;
      }

      Serial.print("TRACE > handleEvent(): currentProfile -> ");
      Serial.println(currentProfile);
      currentState = STATE_START;
    }

    break;
  case STATE_REFLOW_LANDING:
    switch (eventType)
    {
    case AceButton::kEventPressed:
      switch (button->getPin())
      {
      case BUTTON_PIN1:
        currentState = STATE_START;
        break;
      case BUTTON_PIN2:
        currentState = STATE_REFLOW_STARTED;
        break;
      }
    }

    break;
  case STATE_REFLOW_STARTED:
    if (eventType == AceButton::kEventPressed)
    {
      break;
    }

    break;
  case STATE_REFLOW_FINISHED:
    switch (eventType)
    {
    case AceButton::kEventPressed:
      currentState = STATE_START;
      break;
    }
  }

  if (lastState != currentState)
  {
    requestedRedraw = true;
  }
}