#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "AudioKitHAL.h"
#include "Audio.h"
#include "config.h"

// --- Audio & Web ---
AudioKit kit;
Audio audio;
AsyncWebServer server(80);

// --- Display ---
// 2.23" OLED is often SSD1305. We use a generic SSD1306/SSD1305 constructor for now.
// I2C pins for ESP32-Audio-Kit are usually SDA=33, SCL=32
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 32, /* data=*/ 33);

// --- Station Management ---
#define MAX_STATIONS 10
String stations[MAX_STATIONS];
String stationNames[MAX_STATIONS];
int currentStation = 0;
String currentTitle = "";

// --- Button Input (Resistor Ladder on GPIO 34) ---
#define BUTTON_PIN 34
unsigned long lastButtonPress = 0;

void loadStations() {
  if (!LittleFS.exists("/stations.json")) {
    Serial.println("No stations.json found, using defaults");
    stationNames[0] = "SWR3";
    stations[0] = "https://liveradio.swr.de/sw282p3/swr3/play.mp3";
    stationNames[1] = "1LIVE";
    stations[1] = "http://wdr-1live-live.icecast.wdr.de/wdr/1live/live/mp3/128/stream.mp3";
    return;
  }
  
  File file = LittleFS.open("/stations.json", "r");
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse stations.json");
    return;
  }

  JsonArray array = doc["stations"].as<JsonArray>();
  for (int i = 0; i < MAX_STATIONS && i < array.size(); i++) {
    stationNames[i] = array[i]["name"].as<String>();
    stations[i] = array[i]["url"].as<String>();
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
    
    // Update Display
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawStr(0, 12, stationNames[index].c_str());
    u8g2.sendBuffer();
  }
}

void checkButtons() {
  if (millis() - lastButtonPress < 300) return; // Debounce

  int adcVal = analogRead(BUTTON_PIN);
  if (adcVal > 100) { // Assuming 0 is nothing pressed (pulldown), or adjust logic if pullup
    // TODO: Map ADC values to buttons 0-9
    // Example placeholder thresholds:
    int btn = -1;
    if (adcVal > 4000) btn = 1;
    else if (adcVal > 3500) btn = 2;
    else if (adcVal > 3000) btn = 3;
    else if (adcVal > 2500) btn = 4;
    else if (adcVal > 2000) btn = 5;
    else if (adcVal > 1500) btn = 6;
    else if (adcVal > 1000) btn = 7;
    else if (adcVal > 500) btn = 8;
    
    if (btn != -1) {
      Serial.printf("Button %d pressed (ADC: %d)\n", btn, adcVal);
      playStation(btn - 1);
      lastButtonPress = millis();
    }
  }
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

  server.begin();
}

void setup() {
  Serial.begin(115200);
  
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  
  // Init Display
  Wire.begin(33, 32); // SDA=33, SCL=32 for Audio Kit
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0,10,"Booting...");
  u8g2.sendBuffer();

  // Init AudioKit HAL (Codec)
  auto cfg = kit.defaultConfig();
  cfg.i2s_active = false; // We let ESP32-audioI2S handle the I2S driver!
  kit.begin(cfg);
  
  // Audio configuration
  audio.setPinout(27, 25, 26, 0); // Ai-Thinker A1S needs BCLK, LRC, DOUT and MCLK on GPIO 0!
  audio.setVolume(15); 
  
  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  
  Serial.println("\n--- Starte WLAN-Scan ---");
  int n = WiFi.scanNetworks();
  int targetNetwork = -1;
  int bestRSSI = -1000;
  
  if (n == 0) {
      Serial.println("Keine Netzwerke gefunden!");
  } else {
      Serial.printf("%d Netzwerke gefunden:\n", n);
      for (int i = 0; i < n; ++i) {
          Serial.printf("%2d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
          
          // Suche das stärkste Netz mit dem passenden Namen
          if (WiFi.SSID(i) == String(wifi_ssid)) {
              if (WiFi.RSSI(i) > bestRSSI) {
                  bestRSSI = WiFi.RSSI(i);
                  targetNetwork = i;
              }
          }
      }
  }
  Serial.println("------------------------\n");

  // Fix DNS issues by forcing a reliable DNS Server
  IPAddress dns(8, 8, 8, 8); // Google DNS
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns);

  if (targetNetwork >= 0) {
      Serial.printf("Umgehe Band-Steering! Verbinde gezielt mit BSSID von %s\n", WiFi.SSID(targetNetwork).c_str());
      WiFi.begin(wifi_ssid, wifi_password, 0, WiFi.BSSID(targetNetwork));
  } else {
      // Fallback
      WiFi.begin(wifi_ssid, wifi_password);
  }
  u8g2.drawStr(0, 25, "WiFi connecting...");
  u8g2.sendBuffer();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());
  
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "WiFi Connected");
  u8g2.setCursor(0, 25);
  u8g2.print(WiFi.localIP());
  u8g2.sendBuffer();
  
  loadStations();
  setupWebserver();
  
  delay(2000);
  playStation(0);
}

void loop() {
  audio.loop();
  // checkButtons(); // Temporarily disabled until physical buttons are wired
  
  // Process AudioKit keys (optional)
  // kit.processActions(); // Removed due to API change, we handle buttons manually
}

// Optional Audio callbacks
void audio_showstation(const char *info){
    Serial.print("station_info: ");
    Serial.println(info);
}

void audio_showstreamtitle(const char *info){
    Serial.print("streamtitle: ");
    Serial.println(info);
    currentTitle = String(info);
    
    // Update Display
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawStr(0, 12, stationNames[currentStation].c_str());
    u8g2.setFont(u8g2_font_6x10_tf);
    // Scroll logic or simple display for title
    u8g2.drawStr(0, 28, currentTitle.substring(0, 20).c_str());
    u8g2.sendBuffer();
}
