#pragma once

#include <Arduino.h>

void display_setup();
void display_loop_tick();

/// Always pass exactly 8 ASCII chars (space-padded). Uses 7-segment font where possible.
void display_set_line(const char line[8]);

void display_blink_set(bool on);
bool display_blink_active();
