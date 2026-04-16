#ifdef ESP_PLATFORM

#include "ESP32Board.h"

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

bool ESP32Board::startOTAUpdate(const char* id, const char* wifi_ssid, const char* wifi_password, const char* connection_type, char reply[]) {
  inhibit_sleep = true;   // prevent sleep during OTA
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(); 
    delay(100); 
  }
  if (connection_type && (strcmp(connection_type, "STA") == 0 || strcmp(connection_type, "sta") == 0)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
    delay(100);
    }
  if (WiFi.status() == WL_CONNECTED) {
    sprintf(reply, "Started: http://%s/update", WiFi.localIP().toString().c_str());
  } else {
    sprintf(reply, "WiFi connect failed");
  }
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);
}
else if (connection_type && (strcmp(connection_type, "AP") == 0 || strcmp(connection_type, "ap") == 0)) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("MeshCore-OTA", NULL);
  delay(100);
  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);
}
else {
  sprintf(reply, "Invalid connection type");
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);
  return false;
}
static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(server);    // Start ElegantOTA
  server->begin();

  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, const char* wifi_ssid, const char* wifi_password, const char* connection_type, char reply[]) {
  return false; // not supported
}
#endif

#endif
