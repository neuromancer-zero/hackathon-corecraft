#pragma once

#include <Arduino.h>

void tracker_setup();
void tracker_loop();

/// Begin tracking a new txid (stops any previous session). txid must be 64 hex chars.
bool tracker_start(const char *txid_lower_64);

void tracker_stop();

bool tracker_is_active();

/// JSON line for GET /status (null-terminated, fixed buffer).
const char *tracker_status_json();

/// 0 = none, 1 = done (show success animation), 2 = error animation.
int tracker_poll_ui_event();

/// True once per "confirmed" WebSocket message (for display blink).
bool tracker_consume_display_blink();

const char *tracker_current_txid();
int tracker_last_confirmations();

