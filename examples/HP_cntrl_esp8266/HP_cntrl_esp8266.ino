#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include "HeatPump.h"
#include "WiFiPass.h"
//#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <TelnetStream.h>
#include "esp_log.h"

static const char* TAG = "HPMain";

//SoftwareSerial softUart(32, 33); // RX, TX
#define debugLog TelnetStream

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;
String hostname = "attic_blower";

const char* html = "<html>\n<head>\n<meta name='viewport' content='width=device-width, initial-scale=2'/>\n"
                   "<meta http-equiv='refresh' content='_RATE_; url=/'/>\n"
                   "<style></style>\n"
                   "<body><h3>Heat Pump Demo</h3>TEMP: _ROOMTEMP_\n&deg;F<form autocomplete='off' method='post' action=''>\n<table>\n"
                   "<tr>\n<td>Power:</td>\n<td>\n_POWER_</td>\n</tr>\n"
                   "<tr>\n<td>Mode:</td>\n<td>\n_MODE_</td>\n</tr>\n"
                   "<tr>\n<td>Temp:</td>\n<td>\n_TEMP_</td>\n</tr>"
                   "<tr>\n<td>Fan:</td>\n<td>\n_FAN_</td>\n</tr>\n"
                   "<tr>\n<td>Vane:</td><td>\n_VANE_</td>\n</tr>\n"
                   "<tr>\n<td>WideVane:</td>\n<td>\n_WVANE_</td>\n</tr>\n"
                   "</table>\n<br/><input type='submit' value='Change Settings'/>\n</form><br/><br/>"
                   "<form><input type='submit' name='CONNECT' value='Re-Connect'/>\n</form>\n"
                   "</body>\n</html>\n";

AsyncWebServer server(80);

Adafruit_NeoPixel pixels(1, 27, NEO_GRB + NEO_KHZ800);

HeatPump hp;

#define MY_LOGI(fmt, ...) esp_log_write(ESP_LOG_INFO, TAG, fmt, ##__VA_ARGS__)


void setup() {  
  hp.setSettings({ //set some default settings
    "ON",  /* ON/OFF */
    "FAN", /* HEAT/COOL/FAN/DRY/AUTO */
    26,    /* Between 16 and 31 */
    "4",   /* Fan speed: 1-4, AUTO, or QUIET */
    "3",   /* Air direction (vertical): 1-5, SWING, or AUTO */
    "|"    /* Air direction (horizontal): <<, <, |, >, >>, <>, or SWING */  
    });
  Serial.begin(115200);           // for debug
  Serial1.begin(2400, SERIAL_8N1, 33, 32); //rx and tx pins
  
  LEDcolorRed();
  
  WiFi.mode(WIFI_STA);
  //WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str()); //define hostname
  WiFi.begin(ssid, password);
  debugLog.println("");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debugLog.print(".");
  }
  
  LEDcolorBlue();

  debugLog.begin();
  esp_log_set_vprintf(&_log_vprintf);
  //esp_log_set_vprintf(_log_vprintf);
  //esp_log_set_vprintf(0); // even this runs without error but of course no output

  delay(5000); // delay in case we want to have someone connect
  bool initHP = hp.connect(&Serial1);
  if(initHP){
    LEDcolorGreen();
    esp_log_write(ESP_LOG_INFO, TAG, "HP connected.\n");
  } else {
    esp_log_write(ESP_LOG_INFO, TAG, "HP NOT connected!\n");
  }
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ handle_root(request); });
  server.on("/generate_204", [](AsyncWebServerRequest *request){ handle_root(request); });
  server.onNotFound([](AsyncWebServerRequest *request){
    MY_LOGI("Request URL not found.");
    handle_root(request);
    //request->send(200, "text/plain", "URI Not Found");
  });
  
  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
  MY_LOGI("HTTP server started");
}

void loop() {
  //debugLog.print("Start: ");
  //debugLog.println(millis());
  AsyncElegantOTA.loop();
  //debugLog.print("Async: ");
  //debugLog.println(millis());
  hp.sync();
  
  checkStreamInput();
  esp_log_write(ESP_LOG_INFO, TAG, "test.\n");
}

void handle_root(AsyncWebServerRequest *request){
  MY_LOGI("Connect from %d",request->client()->remoteIP());
  //debugLog.println(request->client()->remoteIP());
  int rate = change_states(request) ? 0 : 60;
  String toSend = html;
  toSend.replace("_RATE_", String(rate));
  String power[2] = {"OFF", "ON"}; 
  toSend.replace("_POWER_", createOptionSelector("POWER", power, 2, hp.getPowerSetting()));
  String mode[5] = {"HEAT", "DRY", "COOL", "FAN", "AUTO"};
  toSend.replace("_MODE_", createOptionSelector("MODE", mode, 5, hp.getModeSetting()));
  String temp[28] = {"88", "87", "86", "85", "84", "83", "82", "81", "80", "79", "78", "77", "76", "75", "74", "73", "72", "71", "70", "69", "68", "67", "66", "65", "64", "63", "62", "61"};
  toSend.replace("_TEMP_", createOptionSelector("TEMP", temp, 28, String(hp.getTemperature()).substring(0,2)));
  String fan[6] = {"AUTO", "QUIET", "1", "2", "3", "4"};
  toSend.replace("_FAN_", createOptionSelector("FAN", fan, 6, hp.getFanSpeed()));
  String vane[7] = {"AUTO", "1", "2", "3", "4", "5", "SWING"};
  toSend.replace("_VANE_", createOptionSelector("VANE", vane, 7, hp.getVaneSetting()));
  String widevane[7] = {"<<", "<", "|", ">", ">>", "<>", "SWING"}; 
  toSend.replace("_WVANE_", createOptionSelector("WIDEVANE", widevane, 7, hp.getWideVaneSetting()));
  toSend.replace("_ROOMTEMP_", String(hp.getRoomTemperature()));
  request->send(200, "text/html", toSend);
  delay(100);
  MY_LOGI("Page served");
}

String encodeString(String toEncode) {
  toEncode.replace("<", "&lt;");
  toEncode.replace(">", "&gt;");
  toEncode.replace("|", "&vert;");
  return toEncode;
}

String createOptionSelector(String name, const String values[], int len, String value) {
  String str = "<select name='" + name + "'>\n";
  for (int i = 0; i < len; i++) {
    String encoded = encodeString(values[i]);
    str += "<option value='";
    str += values[i];
    str += "'";
    str += values[i] == value ? " selected" : "";
    str += ">";
    str += encoded;
    str += "</option>\n";
  }
  str += "</select>\n";
  return str;
}

bool change_states(AsyncWebServerRequest *request) {
  bool updated = false;
  if (request->hasArg("CONNECT")) {
    bool initHP = hp.connect(&Serial1);
    MY_LOGI("hp.connect = %d", initHP);
    //debugLog.println(initHP);
  }
  else {
    if (request->hasArg("POWER")) {
      hp.setPowerSetting(request->arg("POWER").c_str());
      updated = true;
    }
    if (request->hasArg("MODE")) {
      hp.setModeSetting(request->arg("MODE").c_str());
      updated = true;
    }
    if (request->hasArg("TEMP")) {
      hp.setTemperature(request->arg("TEMP").toFloat());
      updated = true;
    }
    if (request->hasArg("FAN")) {
      hp.setFanSpeed(request->arg("FAN").c_str());
      updated = true;
    }
    if (request->hasArg("VANE")) {
      hp.setVaneSetting(request->arg("VANE").c_str());
      updated = true;
    }
    if (request->hasArg("DIR")) {
      hp.setWideVaneSetting(request->arg("WIDEVANE").c_str());
      updated = true;
    }
    char buff[64]; hp.settingsString(&hp.wantedSettings, buff);
    MY_LOGI("Update: %s",buff);
    hp.update(); 
  }
  return updated;
}

void LEDcolorRed(){
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(150, 0, 0));
  pixels.show();
}

void LEDcolorBlue(){
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 150));
  pixels.show();
}

void LEDcolorGreen(){
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 150, 0));
  pixels.show();
}

void checkStreamInput(){
  switch (TelnetStream.read()) {
//    case 'R':
//    TelnetStream.stop();
//    delay(100);
//    ESP.restart();
//      break;
    case 1: // Ctrl + A
      TelnetStream.println("bye bye");
      Serial.println("bye world!");
      TelnetStream.stop();
      break;
  }
}

int _log_vprintf(const char *fmt, va_list args){
  char buff[256];
  int ret = sprintf(buff, fmt, args);
  TelnetStream.write(buff, ret);
  //TelnetStream.println(buff); // is this correct?????
  return ret;
}
