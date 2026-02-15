#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "config.h"
#include "display.h"
#include "api.h"
#include "touch.h"
#include "storage.h"
#include "web_dashboard.h"

// Disable brownout detector (USB-JTAG issue during WiFi init)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

static WiFiMulti wifiMulti;
static PriceData todayData;
static PriceData tomorrowData;
static unsigned long lastFetchTime = 0;
static unsigned long lastDrawTime = 0;
static unsigned long lastTomorrowFetch = 0;
static bool todayLoaded = false;
static bool tomorrowLoaded = false;
static int currentScreen = 0;    // 0=today, 1=tomorrow
static int lastDay = -1;

static const int TOTAL_SCREENS = 3;

// Weekly data for screen 2
static float weeklyAvg[MAX_HISTORY_DAYS];
static char weeklyLabels[MAX_HISTORY_DAYS][6];  // "MM-DD"
static int weeklyCount = 0;

// Dimming
static uint8_t currentBrightness = BRIGHT_LEVEL;
static unsigned long dimWakeUntil = 0;
static bool manualBrightness = false;  // true = web override, skip auto-dimming

// Screensaver
static unsigned long lastTouchTime = 0;
static bool screenSaverOn = false;

// Yesterday comparison
static float yesterdayAvg = -1.0f;

// Web refresh flag
static volatile bool webRefreshRequested = false;
// Screensaver enabled (togglable from web)
static bool screensaverEnabled = true;

// Callbacks for web dashboard
void onWebRefresh() {
    webRefreshRequested = true;
}

void onWebBrightness(uint8_t val) {
    manualBrightness = true;
    currentBrightness = val;
    displaySetBrightness(val);
    Serial.printf("[WEB] Brightness -> %d%% (manual)\n", val);
}

bool connectWiFi() {
    Serial.println("[WIFI] Connecting (WiFiMulti)...");
    drawConnecting();

    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED) {
        if (millis() - start > WIFI_TIMEOUT_MS) {
            Serial.printf("\n[WIFI] Timeout! Status: %d\n", WiFi.status());
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\n[WIFI] Connected to '%s', IP: %s, RSSI: %d\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

void syncTime() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        delay(1000);
        retries++;
    }
    if (retries < 10) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("[NTP] Time: %s\n", buf);
    }
}

void saveTodayToHistory() {
    if (!todayLoaded) return;
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    DayRecord rec;
    strftime(rec.date, sizeof(rec.date), "%Y-%m-%d", &ti);
    rec.minPrice = todayData.minPrice;
    rec.maxPrice = todayData.maxPrice;
    rec.avgPrice = todayData.avgPrice;
    rec.cheapestHour = todayData.cheapestHour;
    storageSaveDay(rec);
}

void loadYesterdayAvg() {
    DayRecord rec;
    if (storageGetYesterday(rec)) {
        yesterdayAvg = rec.avgPrice;
    } else {
        yesterdayAvg = -1.0f;
    }
}

void loadWeeklyData() {
    static DayRecord records[MAX_HISTORY_DAYS];
    int count = storageGetRecent(records, MAX_HISTORY_DAYS);
    weeklyCount = count;
    for (int i = 0; i < count; i++) {
        weeklyAvg[i] = records[i].avgPrice;
        if (strlen(records[i].date) >= 10) {
            strncpy(weeklyLabels[i], records[i].date + 5, 5);
            weeklyLabels[i][5] = '\0';
        } else {
            weeklyLabels[i][0] = '\0';
        }
    }
    Serial.printf("[APP] Weekly data: %d days\n", weeklyCount);
}

void fetchAll() {
    if (wifiMulti.run() != WL_CONNECTED) {
        if (!connectWiFi()) return;
    }

    // Fetch today
    if (fetchPrices(todayData)) {
        todayLoaded = true;
        lastFetchTime = millis();
        saveTodayToHistory();
        loadYesterdayAvg();
    }

    // Try fetching tomorrow
    if (fetchPricesForDate(tomorrowData, 1)) {
        tomorrowLoaded = true;
        lastTomorrowFetch = millis();
        Serial.printf("[API] Tomorrow data: %d periods, min=%.0f at %d:00\n",
                      tomorrowData.totalPeriods, tomorrowData.minPrice, tomorrowData.cheapestHour);
    } else {
        tomorrowLoaded = false;
    }

    Serial.printf("[API] Heap after fetchAll: %u\n", ESP.getFreeHeap());
}

void updateBrightness(int hour) {
    if (manualBrightness) return;  // web override active

    bool nightTime = (hour >= DIM_HOUR_START || hour < DIM_HOUR_END);
    unsigned long now = millis();

    uint8_t target;
    if (nightTime && now > dimWakeUntil) {
        target = DIM_LEVEL;
    } else {
        target = BRIGHT_LEVEL;
    }

    if (target != currentBrightness) {
        currentBrightness = target;
        displaySetBrightness(currentBrightness);
        Serial.printf("[DIM] Brightness -> %d%%\n", currentBrightness);
    }
}

void handleTouch() {
    TouchGesture g = touchPoll();
    if (g == GESTURE_NONE) return;

    unsigned long now = millis();
    lastTouchTime = now;

    // Wake from screensaver
    if (screenSaverOn) {
        screenSaverOn = false;
        lastDrawTime = 0;
        Serial.println("[UI] Screensaver off");
        // Don't process gesture further - just wake up
        return;
    }

    // Wake brightness if dimmed
    struct tm ti;
    if (getLocalTime(&ti)) {
        bool nightTime = (ti.tm_hour >= DIM_HOUR_START || ti.tm_hour < DIM_HOUR_END);
        if (nightTime) {
            dimWakeUntil = now + (DIM_WAKE_SEC * 1000UL);
            displaySetBrightness(BRIGHT_LEVEL);
            currentBrightness = BRIGHT_LEVEL;
        }
    }

    switch (g) {
        case GESTURE_SWIPE_LEFT:
        case GESTURE_SWIPE_UP:
        case GESTURE_TAP:
            // Next screen
            currentScreen = (currentScreen + 1) % TOTAL_SCREENS;
            Serial.printf("[UI] Screen -> %d\n", currentScreen);
            lastDrawTime = 0;
            break;

        case GESTURE_SWIPE_RIGHT:
        case GESTURE_SWIPE_DOWN:
            // Previous screen
            currentScreen = (currentScreen - 1 + TOTAL_SCREENS) % TOTAL_SCREENS;
            Serial.printf("[UI] Screen -> %d\n", currentScreen);
            lastDrawTime = 0;
            break;

        case GESTURE_LONG_PRESS:
            Serial.println("[UI] Force refresh");
            drawConnecting();
            fetchAll();
            lastDrawTime = 0;
            break;

        default:
            break;
    }
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP32 Energy Price Display ===");

    displayInit();
    touchInit();
    storageInit();

    // WiFiMulti
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // reduce TX to prevent USB-JTAG brownout
    wifiMulti.addAP(WIFI_SSID1, WIFI_PASS1);
    wifiMulti.addAP(WIFI_SSID2, WIFI_PASS2);

    bool wifiOk = false;
    for (int attempt = 0; attempt < 3 && !wifiOk; attempt++) {
        if (attempt > 0) {
            WiFi.disconnect(true);
            delay(2000);
        }
        wifiOk = connectWiFi();
    }

    if (!wifiOk) {
        drawError("WiFi failed");
        delay(30000);
        ESP.restart();
        return;
    }

    syncTime();

    // mDNS
    if (MDNS.begin(OTA_HOSTNAME)) {
        Serial.printf("[mDNS] http://%s.local\n", OTA_HOSTNAME);
    }

    // OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.onStart([]() { drawConnecting(); });
    ArduinoOTA.onEnd([]() { delay(500); ESP.restart(); });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[OTA] Error %u\n", err);
        drawError("OTA error");
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");

    // Web server
    WebContext wctx;
    wctx.today = &todayData;
    wctx.tomorrow = &tomorrowData;
    wctx.todayOk = &todayLoaded;
    wctx.tomorrowOk = &tomorrowLoaded;
    wctx.brightness = &currentBrightness;
    wctx.screenSaverOn = &screenSaverOn;
    wctx.screensaverEnabled = &screensaverEnabled;
    wctx.manualBrightness = &manualBrightness;
    wctx.onRefresh = onWebRefresh;
    wctx.onBrightness = onWebBrightness;
    wctx.onScreensaverToggle = []() { screensaverEnabled = !screensaverEnabled; };
    wctx.onAutoBriToggle = []() {
        manualBrightness = !manualBrightness;
        if (!manualBrightness) {
            // Wracamy na auto - wymuszamy natychmiastowe przeliczenie
            currentBrightness = 0;  // force update in next loop
            Serial.println("[WEB] Brightness -> AUTO");
        } else {
            Serial.printf("[WEB] Brightness -> MANUAL (%d%%)\n", currentBrightness);
        }
    };
    webserverInit(wctx);

    memset(&todayData, 0, sizeof(todayData));
    memset(&tomorrowData, 0, sizeof(tomorrowData));
    fetchAll();
    loadWeeklyData();

    if (!todayLoaded) {
        drawError("API error");
    }

    lastTouchTime = millis();
    Serial.printf("[SYS] Free heap: %d\n", ESP.getFreeHeap());
}

void loop() {
    // OTA + WebServer
    ArduinoOTA.handle();
    webserverHandle();

    // Handle touch input
    handleTouch();

    unsigned long now = millis();

    // Day transition: at midnight, shift tomorrow -> today
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        if (lastDay >= 0 && timeinfo.tm_mday != lastDay) {
            Serial.println("[APP] Day changed!");
            if (tomorrowLoaded) {
                todayData = tomorrowData;
                todayLoaded = true;
            }
            tomorrowLoaded = false;
            currentScreen = 0;
            lastFetchTime = 0;  // trigger fresh fetch
            loadWeeklyData();   // refresh weekly chart
        }
        lastDay = timeinfo.tm_mday;

        // Auto-dimming
        updateBrightness(timeinfo.tm_hour);
    }

    // Web-triggered refresh
    if (webRefreshRequested) {
        webRefreshRequested = false;
        Serial.println("[WEB] Refresh requested");
        fetchAll();
    }

    // Fetch new data every 15 minutes
    if (now - lastFetchTime >= FETCH_INTERVAL_MS || !todayLoaded) {
        fetchAll();
    }

    // Screensaver check (only if enabled)
    if (screensaverEnabled && !screenSaverOn && (now - lastTouchTime >= SCREENSAVER_TIMEOUT_MS)) {
        screenSaverOn = true;
        Serial.println("[UI] Screensaver on");
    }
    // If screensaver was disabled while active, turn it off
    if (!screensaverEnabled && screenSaverOn) {
        screenSaverOn = false;
        lastDrawTime = 0;
        Serial.println("[UI] Screensaver disabled via web");
    }

    // Update display every 1 second
    if (now - lastDrawTime >= DISPLAY_INTERVAL_MS) {
        lastDrawTime = now;

        if (getLocalTime(&timeinfo)) {
            if (screenSaverOn) {
                float price = todayLoaded ? todayData.currentPrice : 0;
                drawClock(timeinfo.tm_hour, timeinfo.tm_min, price);
            } else if (todayLoaded) {
                int mins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
                todayData.currentPeriodIdx = mins / 15;
                if (todayData.currentPeriodIdx >= todayData.totalPeriods)
                    todayData.currentPeriodIdx = todayData.totalPeriods - 1;
                if (todayData.currentPeriodIdx >= 0)
                    todayData.currentPrice = todayData.prices[todayData.currentPeriodIdx];

                switch (currentScreen) {
                    case 0:
                        drawToday(todayData, timeinfo.tm_hour, timeinfo.tm_min,
                                  TOTAL_SCREENS, yesterdayAvg);
                        break;
                    case 1:
                        drawTomorrow(tomorrowData, timeinfo.tm_hour, timeinfo.tm_min,
                                     tomorrowLoaded, TOTAL_SCREENS);
                        break;
                    case 2:
                        if (weeklyCount > 0) {
                            drawWeekly(weeklyAvg, weeklyLabels, weeklyCount,
                                       timeinfo.tm_hour, timeinfo.tm_min, TOTAL_SCREENS);
                        } else {
                            drawError("Brak historii");
                        }
                        break;
                }
            } else {
                drawError("Brak danych");
            }
        }
    }

    // Heap watchdog - log every 30s, restart if critically low
    static unsigned long lastHeapLog = 0;
    static uint32_t minHeapSeen = 999999;
    if (now - lastHeapLog >= 30000) {
        lastHeapLog = now;
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minFree = ESP.getMinFreeHeap();
        if (freeHeap < minHeapSeen) minHeapSeen = freeHeap;
        Serial.printf("[SYS] Heap: %u B (min ever: %u, session min: %u)\n",
                      freeHeap, minFree, minHeapSeen);
        if (freeHeap < 15000) {
            Serial.println("[SYS] Heap critical - restarting!");
            delay(100);
            ESP.restart();
        }
    }

    delay(100);
}
