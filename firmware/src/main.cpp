#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "net.h"
#include "tracker.h"
#include "web.h"

enum class UiMode { Idle, DoneMsg, ErrTxMsg, ErrApiMsg };

static UiMode g_mode = UiMode::Idle;
static uint32_t g_mode_until_ms = 0;
static uint32_t g_next_idle_poll_ms = 0;
/// After each successful idle poll, this reflects which metric was just refreshed / should be shown.
static bool g_display_is_price = true;
static bool g_idle_next_poll_is_price = true;

static double g_last_price_usd = 0;
static long g_last_height = 0;
static bool g_have_price = false;
static bool g_have_height = false;

static uint32_t g_err_api_until_ms = 0;
static uint32_t g_blank_until_ms = 0;

static void set_line8(const char *s8) {
  char buf[8];
  for (int i = 0; i < 8; i++) {
    buf[i] = s8[i] ? s8[i] : ' ';
  }
  display_set_line(buf);
}

static void fmt_price8(char out[8], double usd) {
  // Prefer compact 7-seg friendly symbols: b = btc-ish prefix.
  if (usd >= 1000000.0) {
    unsigned long m = (unsigned long)(usd / 1000000.0);
    snprintf((char *)out, 9, "b%lum", m);
  } else if (usd >= 100000.0) {
    unsigned long k = (unsigned long)(usd / 1000.0);
    snprintf((char *)out, 9, "b%luk ", k);
  } else {
    int v = (int)usd;
    snprintf((char *)out, 9, "b %6d", v);
  }
  for (int i = 0; i < 8; i++) {
    if (out[i] == '\0') {
      out[i] = ' ';
    }
  }
}

static void fmt_block8(char out[8], long h) {
  snprintf((char *)out, 9, "bL%6ld", h);
  for (int i = 0; i < 8; i++) {
    if (out[i] == '\0') {
      out[i] = ' ';
    }
  }
}

static void fmt_track8(char out[8]) {
  const char *t = tracker_current_txid();
  int c = tracker_last_confirmations();
  if (c < 0) {
    c = 0;
  }
  snprintf((char *)out, 9, "tr%.4s%02d", t + 60, c % 100);
  for (int i = 0; i < 8; i++) {
    if (out[i] == '\0') {
      out[i] = ' ';
    }
  }
}

static void idle_poll() {
  uint32_t now = millis();
  if (now < g_next_idle_poll_ms) {
    return;
  }
  g_next_idle_poll_ms = now + POLL_INTERVAL_MS;

  if (g_idle_next_poll_is_price) {
    double p = 0;
    if (net_fetch_price(&p)) {
      g_last_price_usd = p;
      g_have_price = true;
      g_display_is_price = true;
    } else {
      g_err_api_until_ms = now + 2000;
    }
  } else {
    long h = 0;
    if (net_fetch_tip(&h)) {
      g_last_height = h;
      g_have_height = true;
      g_display_is_price = false;
    } else {
      g_err_api_until_ms = now + 2000;
    }
  }
  g_idle_next_poll_is_price = !g_idle_next_poll_is_price;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  display_setup();
  net_setup();
  tracker_setup();
  web_setup();

  g_next_idle_poll_ms = millis() + 500;
}

void loop() {
  uint32_t now = millis();

  net_loop();
  web_loop();
  tracker_loop();
  display_loop_tick();

  int ev = tracker_poll_ui_event();
  if (ev == 1) {
    g_mode = UiMode::DoneMsg;
    g_mode_until_ms = now + DONE_DISPLAY_MS;
    display_blink_set(true);
    set_line8("donE !! ");
  } else if (ev == 2) {
    g_mode = UiMode::ErrTxMsg;
    g_mode_until_ms = now + ERR_DISPLAY_MS;
    display_blink_set(false);
    set_line8("Err tHE ");
  }

  if (g_mode == UiMode::DoneMsg || g_mode == UiMode::ErrTxMsg) {
    if (now >= g_mode_until_ms) {
      g_mode = UiMode::Idle;
      display_blink_set(false);
    } else {
      // Done uses blink handled in display_loop_tick; Err is static.
      if (g_mode == UiMode::ErrTxMsg) {
        set_line8("Err tHE ");
      }
      return;
    }
  }

  if (tracker_consume_display_blink()) {
    g_blank_until_ms = now + 150;
  }
  if (g_blank_until_ms != 0 && now < g_blank_until_ms) {
    set_line8("        ");
    return;
  }
  g_blank_until_ms = 0;

  if (!net_is_connected()) {
    display_blink_set(false);
    set_line8("no nEt   ");
    return;
  }

  if (now < g_err_api_until_ms) {
    set_line8("Err Apl ");
    return;
  }

  if (tracker_is_active()) {
    display_blink_set(false);
    char tline[8];
    fmt_track8(tline);
    set_line8(tline);
    return;
  }

  idle_poll();

  char line[8];
  if (g_display_is_price) {
    if (g_have_price) {
      fmt_price8(line, g_last_price_usd);
    } else {
      memcpy(line, "b ------", 8);
    }
  } else {
    if (g_have_height) {
      fmt_block8(line, g_last_height);
    } else {
      memcpy(line, "bL------", 8);
    }
  }
  display_blink_set(false);
  set_line8(line);
}
