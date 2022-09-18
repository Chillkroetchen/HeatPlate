// CS = 15
// RESET = 2
// DC = 0
// SDI (MOSI) = 4
// SCK = 16

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <max6675.h>

#define TFT_CS 5
#define TFT_RST 2
#define TFT_DC 0
#define TFT_MOSI 23 // Data out
#define TFT_SCLK 18 // Clock out

#define touch_Interrupt 36
#define touch_Data 19
#define touch_CS 17
#define touch_CLK 18

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#define backgroundColor 0x0820
#define textColor 0xFFFF

#define TEMP_SO 26
#define TEMP_CS1 33
#define TEMP_CS2 14
#define TEMP_CS3 27
#define TEMP_SCK 25

MAX6675 TEMP1(TEMP_SCK, TEMP_CS1, TEMP_SO);
MAX6675 TEMP2(TEMP_SCK, TEMP_CS2, TEMP_SO);
MAX6675 TEMP3(TEMP_SCK, TEMP_CS3, TEMP_SO);

// Button definitions
#define button1 32
#define button2 35
#define button3 34
#define button4 39
int buttonPins[4] = {32, 35, 34, 39};
int buttonState[4] = {0, 0, 0, 0};
int lastButtonState[4] = {0, 0, 0, 0};
int buttonPressed[4] = {0, 0, 0, 0};
unsigned long debounceDelay = 50;
unsigned long lastDebounce;

const int tftDelay = 1;
unsigned long lastTFTwrite;
const int MS_TO_S = 1000;    // ms in s conversion factor
const int US_TO_S = 1000000; // us in s conversion factor

int standardLeadfree[5][2]{
    170, 85,
    170, 100,
    260, 45,
    260, 25,
    30, 60};

int fastLeadfree[5][2]{
    150, 30,
    200, 60,
    260, 20,
    260, 20,
    30, 40};

int standardLeaded[5][2]{
    150, 75,
    150, 90,
    220, 35,
    220, 35,
    30, 65};

int fastLeaded[5][2]{
    130, 35,
    180, 30,
    230, 20,
    230, 30,
    30, 50};

void homeScreen(int solderProfile[5][2])
{
  tft.fillScreen(backgroundColor);
  tft.setTextColor(textColor);

  // print chart for temp curve
  tft.setTextSize(1);
  tft.fillRect(201, 144, 114, 91, textColor);
  int tempX[3] = {203, 240, 277};
  int tempY[6] = {146, 164, 178, 192, 206, 220};
  int tempWidth = 36;
  int titleHeight = 16;
  int valueHeight = 13;
  for (int x = 0; x < 3; x++)
  {
    for (int y = 0; y < 6; y++)
    {
      int height;
      if (y == 0)
      {
        height = titleHeight;
      }
      else
      {
        height = valueHeight;
      }
      tft.fillRect(tempX[x], tempY[y], tempWidth, height, backgroundColor);
    }
  }
  const char *tempTitle[3] = {"Point", "Temp", "Time"};
  for (int y = 0; y < 6; y++)
  {
    int titleXoffset = 3;
    int valueXoffset = 10;
    int titleYoffset = 4;
    int valueYoffset = 3;
    for (int x = 0; x < 3; x++)
    {
      if (y == 0)
      {
        tft.setCursor(tempX[x] + titleXoffset, tempY[y] + titleYoffset);
        tft.print(tempTitle[x]);
      }
      else
      {
        tft.setCursor(tempX[x] + valueXoffset, tempY[y] + valueYoffset);
        if (x == 0)
        {
          tft.print(y);
        }
        else
        {
          char msg[3];
          sprintf(msg, "%d", solderProfile[y - 1][x - 1]);
          tft.print(msg);
        }
      }
    }
  }

  // print chart for status
  tft.setTextSize(1);
  tft.fillRect(5, 148, 150, 87, textColor);
  int statX[2] = {7, 109};
  int statY[5] = {150, 167, 184, 201, 218};
  int lWidth = 100;
  int vWidth = 44;
  int statHeight = 15;
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
      tft.fillRect(statX[x], statY[y], width, statHeight, backgroundColor);
    }
  }
  const char *statLable[5] = {"Temp MCU:", "Temp Housing:", "Temp Setpoint:", "Temp Plate:", "Runtime:"};
  for (int i = 0; i < 5; i++)
  {
    tft.setCursor(statX[0] + 3, statY[i] + 4);
    tft.println(statLable[i]);
  }

  // print coordinate system
  int graphCoord[2][2] = {
      12, 132,
      310, 10};
  int X = graphCoord[0][0];
  int Y = graphCoord[0][1];
  float sumTime = 0;
  float maxTemp = solderProfile[2][0];
  // calculate X/Y coordinates of reflow curve in respect to screen space available
  for (int n = 0; n < 5; n++)
  {
    sumTime = sumTime + solderProfile[n][1];
  }
  for (int i = 0; i < 5; i++)
  {
    int deltaX = (solderProfile[i][1] / sumTime) * (graphCoord[1][0] - graphCoord[0][0]);
    int deltaY = (solderProfile[i][0] / maxTemp) * (graphCoord[0][1] - graphCoord[1][1]);
    tft.drawLine(X, Y, X + deltaX, graphCoord[0][1] - deltaY, textColor);
    Y = graphCoord[0][1] - deltaY;
    X = X + deltaX;
  }
  // end of reflow curve calculation

  tft.fillRect(5, 5, 2, 134, textColor);   // y-axis
  tft.fillRect(5, 137, 310, 2, textColor); // x-axis

  // textbox for abort
  tft.fillRect(15, 10, 100, 15, textColor);
  tft.setCursor(17, 14);
  tft.setTextColor(backgroundColor);
  tft.println("Press 2 to abort");
  tft.setTextColor(textColor);

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

void startScreen()
{
  int lineCount = 5;
  tft.fillScreen(backgroundColor);
  tft.setTextSize(1);
  tft.setTextColor(backgroundColor);
  for (int i = 0; i < lineCount; i++)
  {
    const char *content[4] = {"Start Reflow", "Select Profile", "Placeholder", "Placeholder"};
    if (i == 0)
    {
      tft.fillRect(100, 50, 120, 40, textColor);
      tft.setCursor(107, 57);
      tft.println("Selected Profile:");
      tft.setCursor(107, 75);
      tft.println("Standard Unleaded");
    }
    else
    {
      tft.fillRect(100, 95 + 25 * (i - 1), 20, 20, textColor);
      tft.setCursor(108, 101 + 25 * (i - 1));
      tft.println(i);
      tft.fillRect(125, 95 + 25 * (i - 1), 95, 20, textColor);
      tft.setCursor(130, 101 + 25 * (i - 1));
      tft.println(content[i - 1]);
    }
  }
}

void readButtons()
{
  for (int i = 0; i < 4; i++)
  { // Button Reading Routine
    int reading = digitalRead(buttonPins[i]);
    if (reading != lastButtonState[i])
    {
      lastDebounce = millis();
    }
    if ((millis() - lastDebounce) > debounceDelay)
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

void setup(void)
{
  Serial.begin(115200);
  // delay(5000);
  pinMode(button1, INPUT);
  pinMode(button2, INPUT);
  pinMode(button3, INPUT);
  pinMode(button4, INPUT);
  Serial.println(F("Temp Sensor test"));
  tft.init(240, 320); // Init ST7789 320x240
  Serial.println(F("TFT Initialized"));
  tft.invertDisplay(false);
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(45);

  // homeScreen(standardLeaded);
  // delay(5000);
  startScreen();
  // tft.fillScreen(backgroundColor);

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
  if (millis() >= lastTFTwrite + tftDelay * MS_TO_S)
  {
    if (buttonPressed[0] == 1)
    {
      homeScreen(standardLeadfree);
    }
    else if (buttonPressed[1] == 1)
    {
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
}
