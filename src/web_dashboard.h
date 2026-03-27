#pragma once
#include "api.h"

struct WebContext {
    // Dane cenowe
    const PriceData *today;
    const PriceData *tomorrow;
    const bool      *todayOk;
    const bool      *tomorrowOk;

    // Stan wyświetlacza
    uint8_t *brightness;
    bool    *screenSaverOn;
    bool    *screensaverEnabled;
    bool    *manualBrightness;
    float   *priceAlertThreshold;

    // Callbacki
    void (*onRefresh)();
    void (*onBrightness)(uint8_t);
    void (*onScreensaverToggle)();
    void (*onAutoBriToggle)();
    void (*onAlertThreshold)(float);

    // Callback zapisu WiFi (z AP dashboard)
    void (*onWiFiSave)(const char *ssid, const char *pass);
};

// Tryb normalny (STA) – pełny dashboard z danymi cenowymi
void webserverInit(const WebContext &ctx);
void webserverHandle();

// Tryb AP (captive portal) – tylko konfiguracja WiFi + OTA placeholder
void apServerInit(const WebContext &ctx);
void apServerHandle();
