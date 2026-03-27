#include "storage.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── Init ────────────────────────────────────────────────────────────────────

void storageInit() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed");
        return;
    }
    Serial.printf("[FS] LittleFS OK, used %u / %u bytes\n",
                  LittleFS.usedBytes(), LittleFS.totalBytes());
}

// ─── Historia cen ────────────────────────────────────────────────────────────

void storageSaveDay(const DayRecord &rec) {
    JsonDocument doc;
    File f = LittleFS.open(HISTORY_PATH, "r");
    if (f) { deserializeJson(doc, f); f.close(); }

    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    for (JsonObject obj : arr) {
        if (strcmp(obj["d"].as<const char *>(), rec.date) == 0) {
            // Skip write if data unchanged
            if ((int)rec.minPrice == (obj["mn"] | 0) &&
                (int)rec.maxPrice == (obj["mx"] | 0) &&
                (int)rec.avgPrice == (obj["av"] | 0) &&
                rec.cheapestHour  == (obj["ch"] | 0)) {
                Serial.printf("[FS] %s unchanged, skip write\n", rec.date);
                return;
            }
            obj["mn"] = (int)rec.minPrice;
            obj["mx"] = (int)rec.maxPrice;
            obj["av"] = (int)rec.avgPrice;
            obj["ch"] = rec.cheapestHour;
            goto save;
        }
    }
    {
        JsonObject o = arr.add<JsonObject>();
        o["d"]  = rec.date;
        o["mn"] = (int)rec.minPrice;
        o["mx"] = (int)rec.maxPrice;
        o["av"] = (int)rec.avgPrice;
        o["ch"] = rec.cheapestHour;
    }
    while (arr.size() > MAX_HISTORY_DAYS) arr.remove(0);

save:
    f = LittleFS.open(HISTORY_PATH, "w");
    if (f) { serializeJson(doc, f); f.close();
        Serial.printf("[FS] Saved %s, %d days in history\n", rec.date, arr.size()); }
}

bool storageGetYesterday(DayRecord &rec) {
    File f = LittleFS.open(HISTORY_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;
    JsonArray arr = doc.as<JsonArray>();
    int n = arr.size();
    if (n < 2) return false;
    JsonObject obj = arr[n - 2];
    strlcpy(rec.date, obj["d"] | "", sizeof(rec.date));
    rec.minPrice    = obj["mn"] | 0;
    rec.maxPrice    = obj["mx"] | 0;
    rec.avgPrice    = obj["av"] | 0;
    rec.cheapestHour = obj["ch"] | 0;
    Serial.printf("[FS] Yesterday %s: avg=%.0f\n", rec.date, rec.avgPrice);
    return true;
}

int storageGetRecent(DayRecord *records, int maxCount) {
    File f = LittleFS.open(HISTORY_PATH, "r");
    if (!f) return 0;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return 0; }
    f.close();
    JsonArray arr = doc.as<JsonArray>();
    int n = arr.size();
    int start = (n > maxCount) ? n - maxCount : 0;
    int count = 0;
    for (int i = start; i < n; i++) {
        JsonObject obj = arr[i];
        strlcpy(records[count].date, obj["d"] | "", sizeof(records[count].date));
        records[count].minPrice    = obj["mn"] | 0;
        records[count].maxPrice    = obj["mx"] | 0;
        records[count].avgPrice    = obj["av"] | 0;
        records[count].cheapestHour = obj["ch"] | 0;
        count++;
    }
    return count;
}

// ─── WiFi credentials ────────────────────────────────────────────────────────

bool storageLoadWiFiCreds(WiFiCreds &creds) {
    File f = LittleFS.open(WIFI_CREDS_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;
    const char *s = doc["ssid"] | "";
    const char *p = doc["pass"] | "";
    if (strlen(s) == 0) return false;
    strlcpy(creds.ssid, s, sizeof(creds.ssid));
    strlcpy(creds.pass, p, sizeof(creds.pass));
    Serial.printf("[FS] Loaded WiFi creds: ssid='%s'\n", creds.ssid);
    return true;
}

bool storageSaveWiFiCreds(const char *ssid, const char *pass) {
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    File f = LittleFS.open(WIFI_CREDS_PATH, "w");
    if (!f) { Serial.println("[FS] Cannot write wifi.json"); return false; }
    serializeJson(doc, f);
    f.close();
    Serial.printf("[FS] WiFi creds saved: ssid='%s'\n", ssid);
    return true;
}

void storageDeleteWiFiCreds() {
    if (LittleFS.exists(WIFI_CREDS_PATH)) {
        LittleFS.remove(WIFI_CREDS_PATH);
        Serial.println("[FS] WiFi creds deleted");
    }
}
