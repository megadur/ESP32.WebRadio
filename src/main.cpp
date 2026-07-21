#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "PCF8574.h"
#include "AudioKitHAL.h"
#include "Audio.h"
#include "config.h"

// --- Audio & Web ---
AudioKit kit;
Audio audio;
AsyncWebServer server(80);

// --- Display ---
TFT_eSPI tft = TFT_eSPI(); // Pins are defined in platformio.ini

// --- Button Input (PCF8574 on I2C) ---
PCF8574 pcf(0x20);
bool pcfConnected = false;

// --- Station Management ---
#define MAX_STATIONS 10
String stations[MAX_STATIONS];
String stationNames[MAX_STATIONS];
int currentStation = 0;
String currentTitle = "";

// #define BUTTON_PIN 34 (No longer used)
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
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK); // Retro Amber Look
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print(stationNames[index]);
  }
}

void checkButtons() {
  if (!pcfConnected) return; // Skip if no hardware connected
  
  if (millis() - lastButtonPress < 300) return; // Debounce

  int pressedBtn = -1;

  // We scan the 4 columns (P0, P1, P2, P3).
  // The rows (P4, P5, P6) are configured as inputs.
  // Because diodes point from Switch to Row, we pull the Row LOW and drive Column HIGH.
  // Actually, PCF8574 has weak pull-ups.
  // We write 0 to one Row at a time to sink current, and read the Columns.
  
  // Make sure all columns are inputs with pullups (write 1)
  uint8_t writeState = 0xFF; // All HIGH
  
  for (int row = 0; row < 3; row++) {
    int rowPin = row + 4; // P4, P5, P6
    
    // Pull the current row LOW
    writeState &= ~(1 << rowPin);
    pcf.write8(writeState);
    
    // Small delay for PCF8574 to settle
    delayMicroseconds(100);
    
    // Read columns (P0 to P3)
    uint8_t readVal = pcf.read8();
    
    // If a button is pressed, the column (P0-P3) will be pulled LOW by the Row
    if ((readVal & (1 << 0)) == 0) { // Col 0 (Pin 1)
      if (row == 0) pressedBtn = 4; // R1
      if (row == 1) pressedBtn = 8; // R2
    }
    if ((readVal & (1 << 1)) == 0) { // Col 1 (Pin 5)
      if (row == 0) pressedBtn = 1;
      if (row == 1) pressedBtn = 5;
      if (row == 2) pressedBtn = 9;
    }
    if ((readVal & (1 << 2)) == 0) { // Col 2 (Pin 6)
      if (row == 0) pressedBtn = 2;
      if (row == 1) pressedBtn = 6;
      if (row == 2) pressedBtn = 0;
    }
    if ((readVal & (1 << 3)) == 0) { // Col 3 (Pin 7)
      if (row == 0) pressedBtn = 3;
      if (row == 1) pressedBtn = 7;
      // row 2 is Store (ignore)
    }
    
    // Restore the row HIGH
    writeState |= (1 << rowPin);
    pcf.write8(writeState);
  }

  if (pressedBtn != -1) {
    int index = (pressedBtn == 0) ? 9 : pressedBtn - 1; // Map 1-9 to 0-8, 0 to 9
    Serial.printf("Button %d pressed (Index: %d)\n", pressedBtn, index);
    playStation(index);
    lastButtonPress = millis();
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

  AsyncCallbackJsonWebHandler *playHandler = new AsyncCallbackJsonWebHandler("/api/play", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    int index = jsonObj["index"].as<int>();
    playStation(index);
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });
  server.addHandler(playHandler);

  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    audio.stopSong();
    
    // Update Display
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("Radio gestoppt");
    
    request->send(200, "application/json", "{\"status\":\"success\"}");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  
  // Init Display & I2C
  Wire.begin(33, 32); // SDA=33, SCL=32 for Audio Kit
  
  pcfConnected = pcf.begin();
  if (pcfConnected) {
    pcf.write8(0xFF); // Initialize PCF pins as HIGH (weak pull-up)
  } else {
    Serial.println("PCF8574 nicht gefunden! Tastenmatrix deaktiviert.");
  }
  
  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Booting...");

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
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.print("WiFi connecting...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());
  
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.print("WiFi Connected");
  tft.setCursor(10, 35);
  tft.print(WiFi.localIP().toString());
  
  loadStations();
  setupWebserver();
  
  delay(2000);
  playStation(0);
}

void loop() {
  audio.loop();
  checkButtons(); // Matrix scanning active!
  
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
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print(stationNames[currentStation]);
    
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.print(currentTitle.substring(0, 40)); // Show up to 40 chars
}
