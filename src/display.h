#pragma once

#include "api.h"

void displayInit();
void displaySetBrightness(uint8_t percent);

void drawToday(const PriceData &data, int hour, int minute, int totalScreens,
               float yesterdayAvg = -1.0f);
void drawTomorrow(const PriceData &data, int hour, int minute, bool available, int totalScreens);
void drawWeekly(const float *dailyAvg, const char labels[][6], int count,
                int hour, int minute, int totalScreens);
void drawClock(int hour, int minute, float currentPrice);
void drawConnecting();
void drawError(const char *msg);
