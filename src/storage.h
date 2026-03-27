#pragma once
#include <Arduino.h>

#define MAX_HISTORY_DAYS 14

struct DayRecord {
    char  date[11];     // "2026-02-15"
    float minPrice;
    float maxPrice;
    float avgPrice;
    int   cheapestHour;
};

struct WiFiCreds {
    char ssid[64];
    char pass[64];
};

struct UserSettings {
    uint8_t brightness;          // 0-100
    bool    manualBright;
    bool    screensaverOn;
    float   priceAlertThreshold; // 0 = disabled
};

void   storageInit();

// Historia cen
void   storageSaveDay(const DayRecord &rec);
bool   storageGetYesterday(DayRecord &rec);
int    storageGetRecent(DayRecord *records, int maxCount);

// Zapisane poświadczenia WiFi (z captive portal)
bool   storageLoadWiFiCreds(WiFiCreds &creds);
bool   storageSaveWiFiCreds(const char *ssid, const char *pass);
void   storageDeleteWiFiCreds();

// Ustawienia użytkownika (persystencja)
bool   storageLoadSettings(UserSettings &s);
bool   storageSaveSettings(const UserSettings &s);
