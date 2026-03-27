#pragma once

#include <Arduino.h>

struct PriceData {
    float prices[96];       // 96 x 15-min intervals
    float hourlyAvg[24];    // hourly averages
    float currentPrice;
    float minPrice;
    float maxPrice;
    float avgPrice;
    int   totalPeriods;
    int   currentPeriodIdx;
    int   cheapestHour;     // hour with lowest average price
    int   expensiveHour;    // hour with highest average price
    int   cheapWindowStart; // start hour of cheapest consecutive window
    int   cheapWindowLen;   // length in hours of cheapest window
    bool  valid;
};

bool fetchPrices(PriceData &data);               // today
bool fetchPricesForDate(PriceData &data, int dayOffset);  // +1 = tomorrow
