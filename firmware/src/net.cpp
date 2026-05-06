#include "net.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "secrets.h"

static uint32_t g_next_wifi_attempt_ms = 0;
static uint32_t g_wifi_backoff_ms = 1000;

void net_setup() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  g_next_wifi_attempt_ms = millis() + g_wifi_backoff_ms;
}

void net_loop() {
  wl_status_t s = WiFi.status();
  if (s == WL_CONNECTED) {
    g_wifi_backoff_ms = 1000;
    return;
  }
  uint32_t now = millis();
  if (now < g_next_wifi_attempt_ms) {
    return;
  }
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  g_next_wifi_attempt_ms = now + g_wifi_backoff_ms;
  g_wifi_backoff_ms = min<uint32_t>(g_wifi_backoff_ms * 2, 30000U);
}

bool net_is_connected() { return WiFi.status() == WL_CONNECTED; }

static bool http_get_json(const char *url, DynamicJsonDocument &doc) {
  if (!net_is_connected()) {
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  DeserializationError err = deserializeJson(doc, payload);
  return !err;
}

bool net_fetch_price(double *out_usd) {
  char url[96];
  snprintf(url, sizeof(url), "http://%s:%d/api/price", BACKEND_HOST, BACKEND_HTTP_PORT);
  DynamicJsonDocument doc(256);
  if (!http_get_json(url, doc)) {
    return false;
  }
  if (!doc["usd"].is<double>() && !doc["usd"].is<int>()) {
    return false;
  }
  *out_usd = doc["usd"].as<double>();
  return true;
}

bool net_fetch_tip(long *out_height) {
  char url[96];
  snprintf(url, sizeof(url), "http://%s:%d/api/block/tip", BACKEND_HOST, BACKEND_HTTP_PORT);
  DynamicJsonDocument doc(256);
  if (!http_get_json(url, doc)) {
    return false;
  }
  if (!doc["height"].is<int>() && !doc["height"].is<long>()) {
    return false;
  }
  *out_height = doc["height"].as<long>();
  return true;
}
