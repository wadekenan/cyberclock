#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <WiFi.h> // wifi duh
#include <Adafruit_AHTX0.h> // temperature
#include <ScioSense_ENS160.h> // gas sensor
#include "time.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

// colors
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

// drawing stuff
const int lcdWidth = 320;
const int lcdHeight = 240;
const int leftEdge = 20;
const int edRectSpacing = 30;
const int envDisRectW = 110;
const int envDisRectH = 25;
const int edReadingsX = 140;

JsonDocument doc; // JsonDocument object 

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST); // tft object from Adafruit_ILI9341 class

Adafruit_AHTX0 aht; // declare AHTX0 object

ScioSense_ENS160 ens160(0x53); // ENS160 I2C address 0x53

// global variables 
float curTemp;
float curHum;

unsigned long prevMillis = 0;

bool lastKey0 = HIGH;
bool lastEncBtn = HIGH;
int encLast;
int encCounter = 0;

String prevTimeStr = "";

// enumeration for the different modes
enum UIMode {
  MODE_MENU = 0,
  MODE_MONITOR,
  MODE_WEATHER
};
// current mode initialized to menu
UIMode currentMode = MODE_MENU;
 
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
  drawMenu();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
  }

  // update button pressed bools
  bool key0Pressed = checkButtonPressed(KEY0_PIN, lastKey0);
  bool encBtnPressed = checkButtonPressed(ENC_BTN_PIN, lastEncBtn);

  // current mode to menu
  if (key0Pressed) {
    encCounter = 0;
    currentMode = MODE_MENU;
    drawMenu();
  }

  // mode switching
  switch (currentMode) {
    case MODE_MENU: 
      if (updateEncoder(2)) clearMenuSelections();
      // menu switch case to change selection
      switch (encCounter) {
        case 0:
          tft.drawRect(20, 35, 110, 25, CYAN);
          if (encBtnPressed) 
          {
            drawClockStatics();
            currentMode = MODE_MONITOR;
          }
          break;
        case 2:
          tft.drawRect(leftEdge, 35+40, 110, 25, CYAN);
          if (encBtnPressed) 
          {
            if (retrieveWeather()) {
              drawCurrentWeather();
              drawDailyWeather(0);
              encCounter = 0;
              currentMode = MODE_WEATHER;
            }
            else drawMenu();
          }
          break;
      }
      break;

    case MODE_MONITOR: 
      drawTime();
      drawEnviroData();
      break;

    case MODE_WEATHER:
      // check for encoder update and pass enc counter to draw correct daily weather 
      if (updateEncoder(12) && encCounter%2 == 0) drawDailyWeather(encCounter/2);
      unsigned long curMillis = millis();
      // reretrieve weather every 10 min
      if (curMillis - prevMillis > 600000) // 10 min in millis = 600000
      {
        if (retrieveWeather()) 
        {
          drawCurrentWeather();
          drawDailyWeather(0);
          currentMode = MODE_WEATHER;
        }
        else 
        {
          // if fail, open menu
          currentMode = MODE_MENU;
          drawMenu();
        }
        prevMillis = curMillis;
      }
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
  if (timeinfo.tm_hour > 12)
  {
    timeinfo.tm_hour -= 12;
  }
  else if (timeinfo.tm_hour == 0)
  {
    timeinfo.tm_hour = 12;
  }
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
  curTemp = CtoF(temp.temperature)-2; // temp calibration fix
  tft.setCursor(edReadingsX, 105);
  tft.print(curTemp); tft.print(" F");
  
  curHum = humidity.relative_humidity;
  tft.setCursor(edReadingsX, tft.getCursorY()+edRectSpacing+5);
  tft.print(curHum); tft.print("% rH");

  if (ens160.available()) {
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

bool retrieveWeather() {
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 100);
  tft.print("Retrieving weather...");

  // url for weather data api
  String url = "https://api.open-meteo.com/v1/forecast?latitude=37.97&longitude=-87.56&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,relative_humidity_2m_mean,wind_speed_10m_mean&current=temperature_2m,precipitation,weather_code&timezone=America%2FChicago&wind_speed_unit=mph&temperature_unit=fahrenheit";
  HTTPClient http; // HTTPClient object
  http.begin(url); // connect to website
  int httpResponseCode = http.GET(); // stores servers response status code
  if (httpResponseCode>0) 
  {
    String payload = http.getString(); // store json data as string
    DeserializationError error = deserializeJson(doc, payload); // raw text to json doc obj
    if (!error) // if not empty, error
    {
      tft.fillScreen(BLACK);
      tft.setCursor(20, 100);
      tft.print("Successfully Retrieved!");
      delay(1000);
      tft.fillScreen(BLACK);
      return true;
    }
    else 
    {
      tft.fillScreen(BLACK);
      Serial.print("json fail");
      tft.setCursor(50, 100);
      tft.print("JSON Error");
      delay(1000);
    }
    http.end();
  }
  else 
  {
    tft.fillScreen(BLACK);
    Serial.print("http fail");
    tft.setCursor(50, 100);
    tft.print("HTTP Error");
    delay(1000);
  }
  return false;
}
  

void drawCurrentWeather() {
  int startY = 25;
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(20, startY);
  tft.print("Current");
  tft.drawLine(20, startY+80, lcdWidth-20, startY+80, CYAN);

  // weather code translated
  tft.setCursor(20, startY+55);
  tft.print(getWeatherDesc(doc["current"]["weather_code"].as<int>()));

  // precip in next hr in mm
  tft.setCursor(160, startY+15);
  tft.print("Precip.+1Hr:");
  tft.setCursor(160, startY+35);
  tft.print(doc["current"]["precipitation"].as<int>());
  tft.print(" mm");

  // current temp
  tft.setCursor(20, startY+25);
  tft.setTextSize(3);
  tft.setTextColor(CYAN);
  tft.print(doc["current"]["temperature_2m"].as<float>());
  tft.print(" F");
}

void drawDailyWeather(int x) {
  tft.fillRect(20, 120, lcdWidth-20, 100, BLACK);

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  timeinfo.tm_wday += x;
  if (timeinfo.tm_wday > 6) // wrap days of the week
  {
    timeinfo.tm_wday -= 7;
  }
  Serial.print(timeinfo.tm_wday);
  // print day of week
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 120);
  tft.print(&timeinfo, "%A");

  // weather code
  tft.setCursor(20, lcdHeight-40);
  tft.print(getWeatherDesc(doc["daily"]["weather_code"][x].as<int>()));
  // precip
  tft.setCursor(140, 140);
  tft.print("Precip:");
  tft.print(doc["daily"]["precipitation_probability_max"][x].as<int>());
  tft.print("%");
  // humid
  tft.setCursor(140, 160);
  tft.print("Humid:");
  tft.print(doc["daily"]["relative_humidity_2m_mean"][x].as<int>());
  tft.print("%");
  //pressure
  tft.setCursor(140, 180);
  tft.print("Wind:");
  tft.print(doc["daily"]["wind_speed_10m_mean"][x].as<float>());
  tft.print(" mph");

  // daily min and max
  tft.setTextColor(CYAN);
  tft.setTextSize(3);
  tft.setCursor(20, 175);
  tft.print(doc["daily"]["temperature_2m_min"][x].as<int>());
  tft.print(" F");

  tft.setTextSize(4);
  tft.setCursor(20, 140);
  tft.print(doc["daily"]["temperature_2m_max"][x].as<int>());
  tft.print(" F");
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

bool updateEncoder(int max) {
  int encA = digitalRead(ENC_A_PIN);
  int encB = digitalRead(ENC_B_PIN);
  if (encA != encLast) {
    if (encA != encB) {
      encCounter++;
      if (encCounter > max) encCounter = max;
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
  tft.print("Weather");
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

float CtoF(float temp) {
  float tempF = (temp - 2.5) * 9/5 + 32;
  return tempF;
}

String getWeatherDesc(int code) {
    switch (code) {
        case 0:  return "Clear sky";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: return "Fog";
        case 48: return "Depositing rime fog";
        case 51: return "Light drizzle";
        case 53: return "Moderate drizzle";
        case 55: return "Dense drizzle";
        case 56: return "Light freezing drizzle";
        case 57: return "Dense freezing drizzle";
        case 61: return "Slight rain";
        case 63: return "Moderate rain";
        case 65: return "Heavy rain";
        case 66: return "Light freezing rain";
        case 67: return "Heavy freezing rain";
        case 71: return "Slight snowfall";
        case 73: return "Moderate snowfall";
        case 75: return "Heavy snowfall";
        case 77: return "Snow grains";
        case 80: return "Slight rain showers";
        case 81: return "Moderate rain showers";
        case 82: return "Violent rain showers";
        case 85: return "Slight snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm";
        case 96: return "Thunderstorm, slight hail";
        case 97: return "Heavy thunderstorm";
        case 99: return "Thunderstorm, heavy hail";
        default: return "Unknown Weather"; 
    }
}
