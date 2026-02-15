#pragma once

#include <Arduino.h>

#define MAX_HISTORY_DAYS 14

struct DayRecord {
    char date[11];      // "2026-02-15"
    float minPrice;
    float maxPrice;
    float avgPrice;
    int cheapestHour;
};

void storageInit();
void storageSaveDay(const DayRecord &rec);
bool storageGetYesterday(DayRecord &rec);
int storageGetRecent(DayRecord *records, int maxCount);
String storageGetHistoryJson();
