#pragma once

#include <Arduino.h>

// MAX7219 (FC-16 style 8 digits): DIN=D7(GPIO13), CLK=D5(GPIO14), CS=D8(GPIO15)
#ifndef PIN_MAX7219_CS
#define PIN_MAX7219_CS D8
#endif

#ifndef POLL_INTERVAL_MS
#define POLL_INTERVAL_MS 15000U
#endif

#ifndef DONE_DISPLAY_MS
#define DONE_DISPLAY_MS 5000U
#endif

#ifndef ERR_DISPLAY_MS
#define ERR_DISPLAY_MS 3000U
#endif

#ifndef BACKEND_HTTP_PORT
#define BACKEND_HTTP_PORT 8000
#endif

#ifndef BACKEND_WS_PORT
#define BACKEND_WS_PORT 8000
#endif
