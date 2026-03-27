#include "api.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Persistent TLS client - avoids heap fragmentation from repeated alloc/free
// Stays allocated after first use (~40KB permanent, but no fragmentation)
static WiFiClientSecure *tlsClient = nullptr;

static void ensureTlsClient() {
    if (!tlsClient) {
        tlsClient = new WiFiClientSecure();
        tlsClient->setInsecure();
        tlsClient->setTimeout(15000);
        Serial.printf("[API] TLS client created, heap: %u\n", ESP.getFreeHeap());
    }
}

static bool doFetch(PriceData &data, const char *dateStr) {
    // Build URL on stack (avoid String heap fragmentation)
    char url[160];
    char path[128];
    snprintf(path, sizeof(path), PSE_API_PATH, dateStr);
    snprintf(url, sizeof(url), "https://%s%s", PSE_API_HOST, path);

    Serial.printf("[API] Fetching: %s\n", url);
    Serial.printf("[API] Heap before: %u (min: %u)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());

    ensureTlsClient();

    HTTPClient https;
    https.setTimeout(15000);
    https.useHTTP10(true);  // Required for ArduinoJson streaming
    if (!https.begin(*tlsClient, url)) {
        Serial.println("[API] Connection failed");
        return false;
    }

    int httpCode = https.GET();
    Serial.printf("[API] HTTP %d, heap: %u (min: %u)\n", httpCode, ESP.getFreeHeap(), ESP.getMinFreeHeap());

    if (httpCode != 200) {
        https.end();
        return false;
    }

    // JSON filter: only parse fields we need (saves ~20KB vs full parse)
    JsonDocument filter;
    filter["value"][0]["rce_pln"] = true;
    filter["value"][0]["period"] = true;

    WiFiClient &stream = *https.getStreamPtr();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, stream,
        DeserializationOption::Filter(filter));
    https.end();

    Serial.printf("[API] JSON done, heap: %u (min: %u)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());

    if (err) {
        Serial.printf("[API] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray values = doc["value"].as<JsonArray>();
    data.totalPeriods = 0;
    data.minPrice = 99999.0f;
    data.maxPrice = 0.0f;
    data.avgPrice = 0.0f;
    data.cheapestHour = 0;
    data.expensiveHour = 0;

    float hourlySum[24] = {0};
    int   hourlyCnt[24] = {0};

    for (JsonObject entry : values) {
        if (data.totalPeriods >= MAX_PERIODS) break;

        float price = entry["rce_pln"].as<float>();
        data.prices[data.totalPeriods] = price;

        const char *period = entry["period"];
        int hour = 0;
        if (period) {
            hour = (period[0] - '0') * 10 + (period[1] - '0');
        }

        if (hour >= 0 && hour < 24) {
            hourlySum[hour] += price;
            hourlyCnt[hour]++;
        }

        if (price < data.minPrice) data.minPrice = price;
        if (price > data.maxPrice) data.maxPrice = price;
        data.avgPrice += price;

        data.totalPeriods++;
    }

    if (data.totalPeriods == 0) {
        Serial.println("[API] No data for this date");
        return false;
    }

    data.avgPrice /= data.totalPeriods;

    float cheapest = 99999.0f, expensive = 0.0f;
    for (int h = 0; h < 24; h++) {
        if (hourlyCnt[h] > 0) {
            data.hourlyAvg[h] = hourlySum[h] / hourlyCnt[h];
            if (data.hourlyAvg[h] < cheapest) {
                cheapest = data.hourlyAvg[h];
                data.cheapestHour = h;
            }
            if (data.hourlyAvg[h] > expensive) {
                expensive = data.hourlyAvg[h];
                data.expensiveHour = h;
            }
        } else {
            data.hourlyAvg[h] = 0;
        }
    }

    // Find longest consecutive window of hours below average
    int bestStart = 0, bestLen = 0, curStart = 0, curLen = 0;
    for (int h = 0; h < 24; h++) {
        if (hourlyCnt[h] > 0 && data.hourlyAvg[h] < data.avgPrice) {
            if (curLen == 0) curStart = h;
            curLen++;
            if (curLen > bestLen) {
                bestLen = curLen;
                bestStart = curStart;
            }
        } else {
            curLen = 0;
        }
    }
    data.cheapWindowStart = bestStart;
    data.cheapWindowLen = bestLen;

    data.valid = true;

    Serial.printf("[API] OK: %d periods, avg=%.0f, cheap=%d-%dh, heap=%u\n",
                  data.totalPeriods, data.avgPrice,
                  data.cheapWindowStart, data.cheapWindowStart + data.cheapWindowLen,
                  ESP.getFreeHeap());

    return true;
}

bool fetchPrices(PriceData &data) {
    return fetchPricesForDate(data, 0);
}

bool fetchPricesForDate(PriceData &data, int dayOffset) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[API] Failed to get local time");
        return false;
    }

    timeinfo.tm_mday += dayOffset;
    mktime(&timeinfo);

    char dateStr[12];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);

    if (dayOffset == 0) {
        int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        data.currentPeriodIdx = currentMinutes / 15;
    } else {
        data.currentPeriodIdx = 0;
    }

    bool ok = doFetch(data, dateStr);

    if (ok && dayOffset == 0) {
        if (data.currentPeriodIdx >= data.totalPeriods)
            data.currentPeriodIdx = data.totalPeriods - 1;
        if (data.currentPeriodIdx < 0) data.currentPeriodIdx = 0;
        data.currentPrice = data.prices[data.currentPeriodIdx];
    }

    return ok;
}
