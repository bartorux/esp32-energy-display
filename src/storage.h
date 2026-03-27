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

void   storageInit();

// Historia cen
void   storageSaveDay(const DayRecord &rec);
bool   storageGetYesterday(DayRecord &rec);
int    storageGetRecent(DayRecord *records, int maxCount);
String storageGetHistoryJson();

// Zapisane poświadczenia WiFi (z captive portal)
bool   storageLoadWiFiCreds(WiFiCreds &creds);
bool   storageSaveWiFiCreds(const char *ssid, const char *pass);
void   storageDeleteWiFiCreds();
