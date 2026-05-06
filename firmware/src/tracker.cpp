#include "tracker.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "config.h"
#include "secrets.h"

static WebSocketsClient g_ws;

static char g_txid[65];
static char g_status_json[192];
static bool g_active = false;
static int g_last_confs = -1;
static bool g_need_display_blink = false;
static volatile int g_ui_event = 0;  // NOLINT(runtime/int)

static void build_path(char *out, size_t out_sz, const char *txid) {
  snprintf(out, out_sz, "/ws/tx/%s", txid);
}

static bool is_hex64(const char *s) {
  if (strlen(s) != 64) {
    return false;
  }
  for (int i = 0; i < 64; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

static void set_status_simple(const char *msg) {
  DynamicJsonDocument doc(256);
  doc["line"] = msg;
  serializeJson(doc, g_status_json, sizeof(g_status_json));
}

static void handle_ws_message(uint8_t *payload, size_t length) {
  if (length >= 512) {
    return;
  }
  char buf[512];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  DynamicJsonDocument doc(384);
  if (deserializeJson(doc, buf)) {
    return;
  }
  const char *ev = doc["event"];
  if (!ev) {
    return;
  }

  if (strcmp(ev, "accepted") == 0) {
    set_status_simple("accepted");
  } else if (strcmp(ev, "mempool") == 0) {
    set_status_simple("mempool (0 conf)");
  } else if (strcmp(ev, "confirmed") == 0) {
    int c = doc["confirmations"] | 0;
    char line[64];
    snprintf(line, sizeof(line), "confirmed: %d", c);
    set_status_simple(line);
    g_need_display_blink = true;
    g_last_confs = c;
  } else if (strcmp(ev, "done") == 0) {
    int c = doc["confirmations"] | 0;
    char line[64];
    snprintf(line, sizeof(line), "done (%d conf)", c);
    set_status_simple(line);
    g_ui_event = 1;
    g_ws.disconnect();
    g_active = false;
  } else if (strcmp(ev, "error") == 0) {
    const char *reason = doc["reason"] | "error";
    char line[96];
    snprintf(line, sizeof(line), "error: %s", reason);
    set_status_simple(line);
    g_ui_event = 2;
    g_ws.disconnect();
    g_active = false;
  }
}

static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      if (g_active) {
        set_status_simple("ws disconnected");
        g_active = false;
      }
      break;
    case WStype_CONNECTED:
      set_status_simple("ws connected");
      break;
    case WStype_TEXT:
      handle_ws_message(payload, length);
      break;
    default:
      break;
  }
}

void tracker_setup() {
  g_ws.onEvent(ws_event);
  g_status_json[0] = '{';
  g_status_json[1] = '}';
  g_status_json[2] = '\0';
}

void tracker_loop() { g_ws.loop(); }

bool tracker_start(const char *txid) {
  if (!is_hex64(txid)) {
    return false;
  }
  strncpy(g_txid, txid, sizeof(g_txid) - 1);
  g_txid[sizeof(g_txid) - 1] = '\0';
  for (size_t i = 0; i < strlen(g_txid); i++) {
    if (g_txid[i] >= 'A' && g_txid[i] <= 'F') {
      g_txid[i] = (char)(g_txid[i] - 'A' + 'a');
    }
  }

  g_ws.disconnect();
  g_ui_event = 0;
  g_active = true;
  g_last_confs = -1;
  char path[96];
  build_path(path, sizeof(path), g_txid);
#if defined(DEBUG_ESP_PORT)
  DEBUG_ESP_PORT.printf("WS begin host=%s port=%d path=%s\n", BACKEND_HOST, BACKEND_WS_PORT, path);
#endif
  g_ws.begin(BACKEND_HOST, BACKEND_WS_PORT, path);
  set_status_simple("connecting...");
  return true;
}

void tracker_stop() {
  g_ws.disconnect();
  g_active = false;
}

bool tracker_is_active() { return g_active; }

const char *tracker_status_json() { return g_status_json; }

int tracker_poll_ui_event() {
  noInterrupts();
  int e = g_ui_event;
  g_ui_event = 0;
  interrupts();
  return e;
}

bool tracker_consume_display_blink() {
  if (!g_need_display_blink) {
    return false;
  }
  g_need_display_blink = false;
  return true;
}

const char *tracker_current_txid() { return g_txid; }

int tracker_last_confirmations() { return g_last_confs; }

