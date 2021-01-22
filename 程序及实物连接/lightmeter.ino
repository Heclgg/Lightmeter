#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BH1750.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include"FastLED.h"

#define OLED_DC                 11
#define OLED_CS                 12
#define OLED_CLK                8 //10
#define OLED_MOSI               9 //9
#define OLED_RESET              10 //13
Adafruit_SSD1306 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

BH1750 lightMeter;

#define DomeMultiplier          2.17                    // Multiplier when using a white translucid Dome covering the lightmeter
#define MeteringButtonPin       2                       // Metering button pin
#define PlusButtonPin           3                       // Plus button pin
#define MinusButtonPin          4                       // Minus button pin
#define ModeButtonPin           5                       // Mode button pin
#define MenuButtonPin           6                       // ISO button pin
#define MeteringModeButtonPin   7                       // Metering Mode (Ambient / Flash)
//#define PowerButtonPin          2

#define MaxISOIndex             57
#define MaxApertureIndex        70
#define MaxTimeIndex            80
#define MaxNDIndex              13
#define MaxFlashMeteringTime    5000                    // ms

float   lux;
boolean Overflow = 0;                                   // Sensor got Saturated and Display "Overflow"
float   ISOND;
boolean ISOmode = 0;
boolean NDmode = 0;

boolean PlusButtonState;                // "+" button state
boolean MinusButtonState;               // "-" button state
boolean MeteringButtonState;            // Metering button state
boolean ModeButtonState;                // Mode button state
boolean MenuButtonState;                // ISO button state
boolean MeteringModeButtonState;        // Metering mode button state (Ambient / Flash)

boolean ISOMenu = false;
boolean NDMenu = false;
boolean mainScreen = false;

// EEPROM for memory recording
#define ISOIndexAddr        1
#define apertureIndexAddr   2
#define modeIndexAddr       3
#define T_expIndexAddr      4
#define meteringModeAddr    5
#define ndIndexAddr         6

#define defaultApertureIndex 12
#define defaultISOIndex      11
#define defaultModeIndex     0
#define defaultT_expIndex    19

uint8_t ISOIndex =          EEPROM.read(ISOIndexAddr);
uint8_t apertureIndex =     EEPROM.read(apertureIndexAddr);
uint8_t T_expIndex =        EEPROM.read(T_expIndexAddr);
uint8_t modeIndex =         EEPROM.read(modeIndexAddr);
uint8_t meteringMode =      EEPROM.read(meteringModeAddr);
uint8_t ndIndex =           EEPROM.read(ndIndexAddr);

int battVolts;
#define batteryInterval 10000
double lastBatteryTime = 0;

#include "lightmeter.h"

/*WS2812部分*/
#define NUM 64  //LED灯珠个数
#define LED_DT 13  //控制输出的引脚
#define LED_TYPE WS2812 //灯板型号q
#define COLOR_ORDER GRB //RGB灯珠中红色、绿色、蓝色LED的排列顺序

CRGB leds[NUM]; // 建立光带leds

int hVal;   //色调数值
int sVal;   //饱和度数值
int vVal;  //明亮度数值

void setup() 
{  
  pinMode(PlusButtonPin, INPUT_PULLUP);
  pinMode(MinusButtonPin, INPUT_PULLUP);
  pinMode(MeteringButtonPin, INPUT_PULLUP);
  pinMode(ModeButtonPin, INPUT_PULLUP);
  pinMode(MenuButtonPin, INPUT_PULLUP);
  pinMode(MeteringModeButtonPin, INPUT_PULLUP);

  battVolts = getBandgap();  //Determins what actual Vcc is, (X 100), based on known bandgap voltage

  Wire.begin();
  lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE_2);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  display.setTextColor(WHITE);
  display.clearDisplay();

  if (apertureIndex > MaxApertureIndex) {
    apertureIndex = defaultApertureIndex;
  }

  if (ISOIndex > MaxISOIndex) {
    ISOIndex = defaultISOIndex;
  }

  if (T_expIndex > MaxTimeIndex) {
    T_expIndex = defaultT_expIndex;
  }

  if (modeIndex < 0 || modeIndex > 1) {
    // Aperture priority. Calculating shutter speed.
    modeIndex = 0;
  }

  if (meteringMode > 1) {
    meteringMode = 0;
  }

  if (ndIndex > MaxNDIndex) {
    ndIndex = 0;
  }

  lux = getLux();
  refresh();

  LEDS.addLeds<LED_TYPE,LED_DT,COLOR_ORDER>(leds,NUM);    // 初始化光带
//Serial.begin(9600);
}

void loop() 
{  
  ctrlRGB();
  lightmeter();
  delay(500);
}

void ctrlRGB()
{
  hVal =analogRead(A0);
  hVal = map(hVal,0,1023,0,255);
  sVal =analogRead(A1);
  sVal = map(sVal,0,1023,0,255);
  vVal =analogRead(A2);
  vVal = map(vVal,0,1023,0,255);
  delay(10);

  CHSV myHSV(hVal,sVal,vVal);
  fill_solid(leds,NUM,myHSV);
  FastLED.show();
  delay(10);
//  Serial.println(hVal);
//  Serial.println(sVal);
//  Serial.println(vVal);

/*屏幕显示*/
//  display.clearDisplay();
//  display.setTextColor(WHITE);
//  display.setTextSize(1);
//  display.setCursor(10,1);
//  display.print("H:");
//  display.print(hVal);
//  display.setCursor(15,1);
//  display.print("S:");
//  display.print(sVal);
//  display.setCursor(20,1);
//  display.print("V:");
//  display.print(vVal);
}

void lightmeter()
{
  if (millis() >= lastBatteryTime + batteryInterval) {
    lastBatteryTime = millis();
    battVolts = getBandgap();
  }
  
  readButtons();

  menu();

  if (MeteringButtonState == 0) 
  {
    // Save setting if Metering button pressed.
    SaveSettings();

    lux = 0;
    refresh();
    
    if (meteringMode == 0) {
      // Ambient light meter mode.
      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE_2);

      lux = getLux();

      if (Overflow == 1) {
        delay(10);
        getLux();
      }

      refresh();
      delay(200);
    } else if (meteringMode == 1) {
      // Flash light metering
      lightMeter.configure(BH1750::CONTINUOUS_LOW_RES_MODE);

      unsigned long startTime = millis();
      uint16_t currentLux = 0;
      lux = 0;

      while (true) {
        // check max flash metering time
        if (startTime + MaxFlashMeteringTime < millis()) {
          break;
        }

        currentLux = getLux();
        delay(16);
        
        if (currentLux > lux) {
          lux = currentLux;
        }
      }

      refresh();
    }
  }
}
