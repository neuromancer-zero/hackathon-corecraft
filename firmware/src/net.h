#pragma once

#include <Arduino.h>

void net_setup();
void net_loop();

bool net_is_connected();

/// Fetch BTC/USD from backend /api/price. Returns false on failure.
bool net_fetch_price(double *out_usd);

/// Fetch chain tip height from backend /api/block/tip.
bool net_fetch_tip(long *out_height);
