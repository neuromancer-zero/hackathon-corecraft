#include "display.h"

#include <MD_MAX72xx.h>
#include <SPI.h>

#include "config.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 1

static MD_MAX72XX mx(HARDWARE_TYPE, PIN_MAX7219_CS, MAX_DEVICES);

static char g_line[9] = "        ";
static bool g_blink = false;
static bool g_blink_visible = true;
static uint32_t g_blink_next_ms = 0;

void display_setup() {
  SPI.begin();
  mx.begin();
  mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
  mx.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON);
  mx.control(MD_MAX72XX::INTENSITY, 8);
  mx.clear();
}

void display_set_line(const char line[8]) {
  for (int i = 0; i < 8; i++) {
    g_line[i] = line[i];
  }
  g_line[8] = '\0';
  if (!g_blink || g_blink_visible) {
    for (uint8_t col = 0; col < 8; col++) {
      char c = g_line[col];
      if (c == '\0') {
        c = ' ';
      }
      mx.setChar(col, c);
    }
  }
}

void display_blink_set(bool on) {
  g_blink = on;
  g_blink_visible = true;
  g_blink_next_ms = millis();
}

bool display_blink_active() { return g_blink; }

void display_loop_tick() {
  if (!g_blink) {
    return;
  }
  uint32_t now = millis();
  if (now < g_blink_next_ms) {
    return;
  }
  g_blink_next_ms = now + 300;
  g_blink_visible = !g_blink_visible;
  if (g_blink_visible) {
    for (uint8_t col = 0; col < 8; col++) {
      char c = g_line[col];
      mx.setChar(col, c);
    }
  } else {
    mx.clear();
  }
}
