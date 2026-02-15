#pragma once

#include "api.h"
// Renamed from webserver.h to avoid macOS case-insensitive <WebServer.h> clash

struct WebContext {
    const PriceData *today;
    const PriceData *tomorrow;
    const bool *todayOk;
    const bool *tomorrowOk;
    uint8_t *brightness;
    bool *screenSaverOn;
    bool *screensaverEnabled;
    bool *manualBrightness;
    void (*onRefresh)();
    void (*onBrightness)(uint8_t);
    void (*onScreensaverToggle)();
    void (*onAutoBriToggle)();
};

void webserverInit(const WebContext &ctx);
void webserverHandle();
