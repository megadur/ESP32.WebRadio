#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Audio.h"
#include "config.h"

// --- Hardware Config ---
#define USE_DISPLAY 1 // Set to 1 when the OLED is connected!

// --- Audio & Web ---
Audio audio;
AsyncWebServer server(80);

// --- Display ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Button Input ---
// Pins for the 4 OLED buttons on ESP32-S3
#define BTN_K1 12 // Prev
#define BTN_K2 11 // Next
#define BTN_K3 10 // Vol +
#define BTN_K4 9  // Vol -

unsigned long lastButtonPress = 0;
const int debounceDelay = 300;

// --- Station Management ---
#define MAX_STATIONS 10
String stations[MAX_STATIONS];
String stationNames[MAX_STATIONS];
int currentStation = 0;
String currentTitle = "";
int currentVolume = 15;

void updateDisplay(String status = "") {
#if USE_DISPLAY
  Serial.println("updateDisplay started");
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (status.length() > 0) {
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println(status);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.printf("Vol: %d", currentVolume);
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    
    display.setTextSize(2);
    display.setCursor(0, 15);
    display.println(stationNames[currentStation].substring(0, 10)); 
    
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.println(currentTitle.substring(0, 21)); 
    if(currentTitle.length() > 21) {
      display.println(currentTitle.substring(21, 42));
    }
  }
  Serial.println("Calling display.display()...");
  display.display();
  Serial.println("updateDisplay finished");
#endif
}

void loadStations() {
  if (!LittleFS.exists("/stations.json")) {
    Serial.println("No stations.json found, using defaults");
    stationNames[0] = "SWR3";
    stations[0] = "http://liveradio.swr.de/sw282p3/swr3/play.mp3"; // HTTP instead of HTTPS to save CPU on C3
    stationNames[1] = "1LIVE";
    stations[1] = "http://wdr-1live-live.icecast.wdr.de/wdr/1live/live/mp3/128/stream.mp3";
    return;
  }
  
  File file = LittleFS.open("/stations.json", "r");
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (!error) {
    JsonArray array = doc["stations"].as<JsonArray>();
    for (int i = 0; i < MAX_STATIONS && i < array.size(); i++) {
      stationNames[i] = array[i]["name"].as<String>();
      stations[i] = array[i]["url"].as<String>();
    }
  }
}

void saveStations() {
  StaticJsonDocument<2048> doc;
  JsonArray array = doc.createNestedArray("stations");
  
  for (int i = 0; i < MAX_STATIONS; i++) {
    if (stations[i].length() > 0) {
      JsonObject obj = array.createNestedObject();
      obj["name"] = stationNames[i];
      obj["url"] = stations[i];
    }
  }

  File file = LittleFS.open("/stations.json", "w");
  serializeJson(doc, file);
  file.close();
}

void playStation(int index) {
  if (index >= 0 && index < MAX_STATIONS && stations[index].length() > 0) {
    currentStation = index;
    currentTitle = "";
    audio.connecttohost(stations[index].c_str());
    Serial.printf("Playing: %s\n", stationNames[index].c_str());
    updateDisplay();
  }
}

void checkButtons() {
#if USE_DISPLAY
  if (millis() - lastButtonPress < debounceDelay) return;

  if (digitalRead(BTN_K1) == LOW) {
    int newStation = currentStation - 1;
    if (newStation < 0) newStation = 0;
    playStation(newStation);
    lastButtonPress = millis();
  } 
  else if (digitalRead(BTN_K2) == LOW) {
    int newStation = currentStation + 1;
    if (newStation >= MAX_STATIONS || stations[newStation].length() == 0) newStation = currentStation;
    playStation(newStation);
    lastButtonPress = millis();
  }
  else if (digitalRead(BTN_K3) == LOW) {
    if(currentVolume < 21) currentVolume++;
    audio.setVolume(currentVolume);
    updateDisplay();
    lastButtonPress = millis();
  }
  else if (digitalRead(BTN_K4) == LOW) {
    if(currentVolume > 0) currentVolume--;
    audio.setVolume(currentVolume);
    updateDisplay();
    lastButtonPress = millis();
  }
#endif
}

void setupWebserver() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });
  
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/script.js", "text/javascript");
  });

  server.on("/api/stations", HTTP_GET, [](AsyncWebServerRequest *request){
    String json;
    File file = LittleFS.open("/stations.json", "r");
    if (file) {
      json = file.readString();
      file.close();
    } else {
      json = "{\"stations\":[]}";
    }
    request->send(200, "application/json", json);
  });

  AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/api/stations", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    JsonArray array = jsonObj["stations"].as<JsonArray>();
    
    for (int i = 0; i < MAX_STATIONS && i < array.size(); i++) {
      stationNames[i] = array[i]["name"].as<String>();
      stations[i] = array[i]["url"].as<String>();
    }
    
    saveStations();
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });
  server.addHandler(handler);

  AsyncCallbackJsonWebHandler *playHandler = new AsyncCallbackJsonWebHandler("/api/play", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    int index = jsonObj["index"].as<int>();
    playStation(index);
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });
  server.addHandler(playHandler);
  
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    audio.stopSong();
    updateDisplay("Stopped");
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
  }
  
  // Init Buttons
#if USE_DISPLAY
  pinMode(BTN_K1, INPUT_PULLUP);
  pinMode(BTN_K2, INPUT_PULLUP);
  pinMode(BTN_K3, INPUT_PULLUP);
  pinMode(BTN_K4, INPUT_PULLUP);
  
  // Init I2C & OLED (SDA=8, SCL=7)
  Wire.begin(8, 7); 
  Wire.setClock(100000); 
  Wire.setTimeOut(100); // Prevent I2C from hanging the ESP32
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  display.display();
#endif
  updateDisplay("Booting...");

  // Audio configuration for ESP32-S3
  // BCLK=3, LRC=1, DIN=2, MCLK nicht verwendet.
  // Da SCK an Pin 4 gelötet ist, ziehen wir ihn per Software auf GND (LOW):
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  
  audio.setPinout(3, 1, 2); 
  
  audio.setVolume(currentVolume);
  
  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("WebRadio");
  WiFi.setSleep(false); // VERHINDERT VERBINDUNGSABBRÜCHE (Errno 113) BEIM STREAMING
  WiFi.disconnect(true);
  delay(100);
  
  int n = WiFi.scanNetworks();
  int targetNetwork = -1;
  int bestRSSI = -1000;
  
  for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i) == String(wifi_ssid)) {
          if (WiFi.RSSI(i) > bestRSSI) {
              bestRSSI = WiFi.RSSI(i);
              targetNetwork = i;
          }
      }
  }

  IPAddress dns(8, 8, 8, 8);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns);

  if (targetNetwork >= 0) {
      WiFi.begin(wifi_ssid, wifi_password, 0, WiFi.BSSID(targetNetwork));
  } else {
      WiFi.begin(wifi_ssid, wifi_password);
  }
  
  updateDisplay("WiFi connecting...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  updateDisplay("WiFi Connected!\n" + WiFi.localIP().toString());
  
  loadStations();
  setupWebserver();
  
  delay(2000);
  playStation(0);
}

void loop() {
  audio.loop();
  checkButtons();
}

void audio_showstation(const char *info){
    Serial.print("station_info: ");
    Serial.println(info);
}

void audio_showstreamtitle(const char *info){
    currentTitle = String(info);
    updateDisplay();
}
