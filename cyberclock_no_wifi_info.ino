#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <WiFi.h> // wifi duh
#include <Adafruit_AHTX0.h> // temperature
#include <ScioSense_ENS160.h> // gas sensor
#include "time.h"

#define BLACK ILI9341_BLACK  
#define WHITE ILI9341_WHITE
#define CYAN 0x07FF  
#define PURPLE 0xF81F
#define GREY 0x4208
#define LIME_GREEN 0x07E0
#define YELLOW 0xFFE0
#define ORANGE 0xFD20
#define RED 0xF800

// lcd pins
#define TFT_CS 9
#define TFT_DC 8
#define TFT_RST 7
#define TFT_MOSI 6
#define TFT_SCLK 5

#define ENC_A_PIN 10
#define ENC_B_PIN 20
#define ENC_BTN_PIN 21
#define KEY0_PIN 0

// sensor pins
#define SDA_PIN 1
#define SCL_PIN 2

#define BUZZ_PIN 3


// wifi and time info
const char* ssid = "network";
const char* password = "password";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5 * 3600;   
const int   daylightOffset_sec = 0;

const int leftEdge = 20;
const int edRectSpacing = 30;
const int envDisRectW = 110;
const int envDisRectH = 25;
const int edReadingsX = 140;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST); // tft object from Adafruit_ILI9341 class

Adafruit_AHTX0 aht; // declare AHTX0 object

ScioSense_ENS160 ens160(0x53); // ENS160 I2C address 0x53

// global variables 
float curTemp;
float curHum;

bool lastKey0 = HIGH;
bool lastEncBtn = HIGH;
int encLast;
int encCounter = 0;

String prevTimeStr = "";

enum UIMode {
  MODE_MENU = 0,
  MODE_MONITOR
};
UIMode currentMode = MODE_MONITOR;

void setup() {
  Serial.begin(115200);
  delay(500);

  // set pin modes
  pinMode(KEY0_PIN, INPUT_PULLUP);
  pinMode(ENC_BTN_PIN, INPUT_PULLUP);
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  // tft stuff
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1); // 320x240 landscape dimensions, but still portrait for some reason 
  uint8_t madctl = 0x80; // 8-bit variable 10000000
  tft.sendCommand(ILI9341_MADCTL, &madctl, 1); // sends raw command to ILI9341_MADCTL at address, setting bit 7 to 1
  // madctl: memory access control (why ad? and not ac idk)

  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);

  Wire.begin(SDA_PIN, SCL_PIN); // starts the I2C communication
  if (!aht.begin()) Serial.println("AHT21 not found");

  ens160.begin(); 
  ens160.setMode(ENS160_OPMODE_STD);

  //attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), updateEncoder, CHANGE);
  //attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), updateEncoder, CHANGE);
  encLast = digitalRead(ENC_A_PIN);

  connectWifiAndSyncTime();

  //tft.fillScreen(BLACK);
  //drawGrid();
  drawClockStatics();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
  }

  /*uint16_t newTVOC = ens160.getTVOC();
  uint16_t newCO2  = ens160.geteCO2();
  if (newTVOC != 0xFFFF) curTVOC = newTVOC;
  if (newCO2  != 0xFFFF) curECO2 = newCO2;*/
  bool key0Pressed = checkButtonPressed(KEY0_PIN, lastKey0);
  bool encBtnPressed = checkButtonPressed(ENC_BTN_PIN, lastEncBtn);

  if (key0Pressed) {
    currentMode = MODE_MENU;
    drawMenu();
  }

  switch (currentMode) {
    case MODE_MENU: 
      if (updateEncoder()) clearMenuSelections();
      switch (encCounter) {
        case 0:
          tft.drawRect(20, 35, 110, 25, CYAN);
          if (encBtnPressed) {
            drawClockStatics();
            currentMode = MODE_MONITOR;
          }
          break;
        case 2:
          tft.drawRect(leftEdge, 35+40, 110, 25, CYAN);
          break;
      }
      break;

    case MODE_MONITOR: 
      //Serial.print(MODE_MONITOR);
      drawTime();
      drawEnviroData();
      break;
  }
  //drawEnsData();
  //delay(200);
}

void connectWifiAndSyncTime() {
  WiFi.begin(ssid, password);
  tft.setCursor(50, 100);
  tft.println("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }

  tft.fillScreen(BLACK);
  tft.setCursor(100, 100);
  tft.println("Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  delay(1500);

}

void drawTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  tft.setTextSize(2);
  tft.setTextColor(WHITE, BLACK);
  tft.setCursor(leftEdge, 30);
  tft.print(&timeinfo, "%A, ");
  tft.print(timeinfo.tm_mon+1);
  tft.print(&timeinfo, "/%d/%Y");
  tft.setTextSize(4);
  tft.setTextColor(CYAN, BLACK);
  tft.setCursor(leftEdge, 55);
  tft.println(&timeinfo, "%H:%M:%S");
}

void drawClockStatics() {
  tft.fillScreen(BLACK);

  tft.setTextSize(2);
  tft.setTextColor(WHITE, BLACK);
  tft.drawRect(leftEdge, 100, envDisRectW, envDisRectH, WHITE);
  tft.setCursor(leftEdge+5, 105);
  tft.print("Temp  ");
  tft.setTextColor(CYAN, BLACK);
  tft.print("->");

  tft.setTextColor(WHITE, BLACK);
  tft.drawRect(leftEdge, tft.getCursorY()+edRectSpacing, envDisRectW, envDisRectH, WHITE);
  tft.setCursor(leftEdge+5, tft.getCursorY()+edRectSpacing+5);
  tft.print("Humid "); 
  tft.setTextColor(CYAN, BLACK);
  tft.print("->");

  tft.setTextColor(WHITE, BLACK);
  tft.drawRect(leftEdge, tft.getCursorY()+edRectSpacing, envDisRectW, envDisRectH, WHITE);
  tft.setCursor(leftEdge+5, tft.getCursorY()+edRectSpacing+5);
  tft.print("TVOC  "); 
  tft.setTextColor(CYAN, BLACK);
  tft.print("->");

  tft.setTextColor(WHITE, BLACK);
  tft.drawRect(leftEdge, tft.getCursorY()+edRectSpacing, envDisRectW, envDisRectH, WHITE);
  tft.setCursor(leftEdge+5, tft.getCursorY()+edRectSpacing+5);
  tft.print("eCO2  "); 
  tft.setTextColor(CYAN, BLACK);
  tft.print("->");
}

void drawAQIMeter() {

}

void drawEnviroData() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity,
               &temp); // set temp and humidity objects with fresh data
  tft.setTextSize(2);
  tft.setTextColor(WHITE, BLACK);
  curTemp = (temp.temperature - 2.5) * 9/5 + 32;
  tft.setCursor(edReadingsX, 105);
  tft.print(curTemp); tft.print(" F");
  
  curHum = humidity.relative_humidity;
  tft.setCursor(edReadingsX, tft.getCursorY()+edRectSpacing+5);
  tft.print(curHum); tft.print("% rH");

  if (ens160.available()) {
    //Serial.print("Air quality connected!!");
    ens160.set_envdata(curTemp, curHum);
    ens160.measure(true);
    //Serial.print("Rating: ");
    //Serial.println(ens160.getAQI());
    tft.setCursor(edReadingsX, tft.getCursorY()+edRectSpacing+5);
    tft.print(ens160.getTVOC()); tft.print(" ppb ");

    tft.setCursor(edReadingsX, tft.getCursorY()+edRectSpacing+5);
    tft.print(ens160.geteCO2()); tft.print(" ppm ");
  }
  else {
    Serial.print("Air quality failure to connect");
  }
}

bool checkButtonPressed(uint8_t pin, bool &lastState) {
  bool pressed;
  bool curState = digitalRead(pin);
  //Serial.print("Last: ");
  //Serial.println(lastState);
  //Serial.print("Cur: ");
  //Serial.println(curState);
  if (lastState == LOW && curState == HIGH) pressed = true;
  else pressed = false;
  lastState = curState;
  return pressed;
}

bool updateEncoder() {
  int encA = digitalRead(ENC_A_PIN);
  int encB = digitalRead(ENC_B_PIN);
  if (encA != encLast) {
    if (encA != encB) {
      encCounter++;
      if (encCounter > 2) encCounter = 2;
    }
    else { 
      encCounter--;
      if (encCounter < 0) encCounter = 0;
    }
    encLast = encA;
    Serial.print("Count: ");
    Serial.println(encCounter);
    return true;
  }
  else return false;
}

void drawMenu() {
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);

  tft.setCursor(25, 40);
  tft.print("Monitor");
  tft.setCursor(25, tft.getCursorY()+40);
  tft.print("Option 2");
}

void clearMenuSelections() {
  for (int i=0; i<2; i++) {
    tft.drawRect(leftEdge, 35 + (40*i), 110, 25, BLACK);
  }
}

void drawGrid() {
  for (int i=0; i<320; i++) {
    for (int j=0; j<240; j++) {
      if (i%20 == 0) {
        tft.drawPixel(i, j, RED);
      }
      if (j%20 == 0) {
        tft.drawPixel(i, j, RED);
      }
    }
  }
}