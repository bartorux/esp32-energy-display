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

// Wyłącz brownout detector (problem USB-JTAG podczas WiFi init)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ─── Typy trybów pracy ────────────────────────────────────────────────────────

enum class AppMode {
    CONNECTING,   // Trwa łączenie z WiFi
    AP_CONFIG,    // Brak WiFi → tryb AP captive portal
    NORMAL        // Normalna praca po sieci STA
};

static AppMode appMode = AppMode::CONNECTING;

// ─── Stan aplikacji ───────────────────────────────────────────────────────────

static WiFiMulti  wifiMulti;
static PriceData  todayData;
static PriceData  tomorrowData;

static unsigned long lastFetchTime      = 0;
static unsigned long lastDrawTime       = 0;
static unsigned long lastTomorrowFetch  = 0;
static unsigned long apStartTime        = 0;     // kiedy weszliśmy w AP mode

static bool todayLoaded    = false;
static bool tomorrowLoaded = false;
static int  currentScreen  = 0;   // 0=today, 1=tomorrow, 2=weekly
static int  lastDay        = -1;

static const int TOTAL_SCREENS = 3;

// Dane tygodniowe (ekran 2)
static float weeklyAvg[MAX_HISTORY_DAYS];
static char  weeklyLabels[MAX_HISTORY_DAYS][6];
static int   weeklyCount = 0;

// Auto-ściemnianie
static uint8_t       currentBrightness = BRIGHT_LEVEL;
static unsigned long dimWakeUntil      = 0;
static bool          manualBrightness  = false;

// Wygaszacz – DOMYŚLNIE WYŁĄCZONY
// Logika: screensaverEnabled = false → timer nigdy nie odpala
// User włącza go w dashboardzie → timer zaczyna odliczać od ostatniego dotyku
// Wyłączenie w dashboardzie → natychmiastowe wyłączenie wygaszacza
static unsigned long lastTouchTime     = 0;
static bool          screenSaverOn     = false;
static bool          screensaverEnabled = SCREENSAVER_DEFAULT;  // false

// Porównanie z wczoraj
static float yesterdayAvg = -1.0f;

// Flaga odświeżenia z web
static volatile bool webRefreshRequested = false;

// Persystencja ustawień
static bool          settingsDirty     = false;
static unsigned long settingsDirtyTime = 0;
#define SETTINGS_DEBOUNCE_MS 2000

// Trend cenowy
enum PriceTrend { TREND_FLAT = 0, TREND_UP = 1, TREND_DOWN = -1 };
static PriceTrend currentTrend = TREND_FLAT;

// Alert cenowy
static float priceAlertThreshold = 0.0f;  // 0 = disabled
static bool  alertActive         = false;

// ─── Callbacki web dashboard ──────────────────────────────────────────────────

static void markSettingsDirty() {
    settingsDirty     = true;
    settingsDirtyTime = millis();
}

void onWebRefresh() {
    webRefreshRequested = true;
}

void onWebBrightness(uint8_t val) {
    manualBrightness  = true;
    currentBrightness = val;
    displaySetBrightness(val);
    markSettingsDirty();
    Serial.printf("[WEB] Brightness -> %d%% (manual)\n", val);
}

// ─── Ekran na wyświetlaczu podczas AP ─────────────────────────────────────────

static void drawAPMode() {
    unsigned long elapsed = (millis() - apStartTime) / 1000;
    unsigned long remaining = (AP_TIMEOUT_MS / 1000 > elapsed)
                              ? (AP_TIMEOUT_MS / 1000 - elapsed) : 0;
    drawAPSetup(AP_SSID, AP_IP, remaining);
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────

/**
 * Próbuje załadować kredencjały z LittleFS i dodać je do WiFiMulti.
 * Zwraca true jeśli znaleziono zapisany SSID.
 */
static bool loadSavedCreds() {
    WiFiCreds creds;
    if (storageLoadWiFiCreds(creds)) {
        wifiMulti.addAP(creds.ssid, creds.pass);
        Serial.printf("[WIFI] Loaded saved creds: '%s'\n", creds.ssid);
        return true;
    }
    return false;
}

/**
 * Łączy z WiFi przez WiFiMulti. Wyświetla ekran "Łączenie...".
 * Zwraca true przy sukcesie.
 */
static bool connectWiFi() {
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
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    return true;
}

/**
 * Startuje tryb AP (SoftAP + captive portal DNS + serwer konfiguracyjny).
 * Wyświetla informację na ekranie.
 */
static void startAPMode() {
    Serial.println("[AP] Starting Access Point...");
    appMode = AppMode::AP_CONFIG;
    apStartTime = millis();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr);
    delay(200);

    Serial.printf("[AP] SSID: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Ekran informacyjny
    drawAPMode();

    // Serwer captive portal
    WebContext wctx = {};   // AP mode – większość pól null, nie potrzeba
    wctx.onWiFiSave = nullptr;  // restart jest w handleWiFiSave
    apServerInit(wctx);
}

// ─── NTP ──────────────────────────────────────────────────────────────────────

static void syncTime() {
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

// ─── Historia / tygodniowe ────────────────────────────────────────────────────

static void saveTodayToHistory() {
    if (!todayLoaded) return;
    struct tm ti;
    if (!getLocalTime(&ti)) return;
    DayRecord rec;
    strftime(rec.date, sizeof(rec.date), "%Y-%m-%d", &ti);
    rec.minPrice     = todayData.minPrice;
    rec.maxPrice     = todayData.maxPrice;
    rec.avgPrice     = todayData.avgPrice;
    rec.cheapestHour = todayData.cheapestHour;
    storageSaveDay(rec);
}

static void loadYesterdayAvg() {
    DayRecord rec;
    yesterdayAvg = storageGetYesterday(rec) ? rec.avgPrice : -1.0f;
}

// Poprzedni tydzień (do ghost bars)
static float prevWeekAvg[7];
static int   prevWeekCount = 0;
static float weeklyChangePercent = 0.0f;

static void loadWeeklyData() {
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

    // Podział na bieżący tydzień (ostatnie 7) i poprzedni
    prevWeekCount = 0;
    weeklyChangePercent = 0.0f;
    if (count > 7) {
        int curStart = count - 7;
        int prevEnd  = curStart;
        int prevStart = (prevEnd > 7) ? prevEnd - 7 : 0;
        float curSum = 0, prevSum = 0;
        for (int i = curStart; i < count; i++) curSum += records[i].avgPrice;
        prevWeekCount = prevEnd - prevStart;
        for (int i = prevStart; i < prevEnd; i++) {
            prevWeekAvg[i - prevStart] = records[i].avgPrice;
            prevSum += records[i].avgPrice;
        }
        float curAvg  = curSum / 7.0f;
        float prevAvg = prevSum / prevWeekCount;
        if (prevAvg > 0) weeklyChangePercent = ((curAvg - prevAvg) / prevAvg) * 100.0f;
    }

    Serial.printf("[APP] Weekly data: %d days, change: %.1f%%\n", weeklyCount, weeklyChangePercent);
}

// ─── Trend cenowy ────────────────────────────────────────────────────────────

static void computePriceTrend(const PriceData &data, int curHour) {
    if (!todayLoaded || data.totalPeriods == 0) { currentTrend = TREND_FLAT; return; }
    float cur = data.hourlyAvg[curHour];
    if (cur <= 0) { currentTrend = TREND_FLAT; return; }

    // Average of next 1-3 hours
    float sum = 0; int cnt = 0;
    for (int i = 1; i <= 3; i++) {
        int h = curHour + i;
        if (h > 23) break;
        if (data.hourlyAvg[h] > 0) { sum += data.hourlyAvg[h]; cnt++; }
    }
    if (cnt == 0) { currentTrend = TREND_FLAT; return; }

    float future = sum / cnt;
    float diff = future - cur;
    float range = data.maxPrice - data.minPrice;
    float threshold = range * 0.08f;

    if (diff > threshold) currentTrend = TREND_UP;
    else if (diff < -threshold) currentTrend = TREND_DOWN;
    else currentTrend = TREND_FLAT;
}

// ─── Fetch danych z API ───────────────────────────────────────────────────────

static void fetchAll() {
    if (wifiMulti.run() != WL_CONNECTED) {
        if (!connectWiFi()) return;
    }
    if (fetchPrices(todayData)) {
        todayLoaded   = true;
        lastFetchTime = millis();
        saveTodayToHistory();
        loadYesterdayAvg();
    }
    if (fetchPricesForDate(tomorrowData, 1)) {
        tomorrowLoaded    = true;
        lastTomorrowFetch = millis();
        Serial.printf("[API] Tomorrow: %d periods, min=%.0f at %d:00\n",
                      tomorrowData.totalPeriods, tomorrowData.minPrice, tomorrowData.cheapestHour);
    } else {
        tomorrowLoaded = false;
    }
    Serial.printf("[API] Heap after fetchAll: %u\n", ESP.getFreeHeap());
}

// ─── Auto-ściemnianie ─────────────────────────────────────────────────────────

static void updateBrightness(int hour) {
    if (manualBrightness) return;
    bool nightTime = (hour >= DIM_HOUR_START || hour < DIM_HOUR_END);
    unsigned long now = millis();
    uint8_t target = (nightTime && now > dimWakeUntil) ? DIM_LEVEL : BRIGHT_LEVEL;
    if (target != currentBrightness) {
        currentBrightness = target;
        displaySetBrightness(currentBrightness);
        Serial.printf("[DIM] Brightness -> %d%%\n", currentBrightness);
    }
}

// ─── Obsługa dotyku ───────────────────────────────────────────────────────────

static void handleTouch() {
    TouchGesture g = touchPoll();
    if (g == GESTURE_NONE) return;

    unsigned long now = millis();

    // Wygaszacz: każdy dotyk go wybudza i resetuje timer
    if (screenSaverOn) {
        screenSaverOn = false;
        lastDrawTime  = 0;
        lastTouchTime = now;
        Serial.println("[UI] Screensaver off (touch)");
        return;  // tylko wybudzamy, nie przetwarzamy gestu dalej
    }

    // Reset timera wygaszacza przy każdym geście
    lastTouchTime = now;

    // Wybudź z auto-ściemniania nocnego
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
            displaySlideOut(-1);  // slide old content left
            currentScreen = (currentScreen + 1) % TOTAL_SCREENS;
            Serial.printf("[UI] Screen -> %d\n", currentScreen);
            lastDrawTime = 0;
            break;

        case GESTURE_SWIPE_RIGHT:
        case GESTURE_SWIPE_DOWN:
            displaySlideOut(1);   // slide old content right
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

// ─── Rysowanie bieżącego ekranu ───────────────────────────────────────────────

static void drawCurrentScreen(const struct tm &timeinfo) {
    if (screenSaverOn) {
        float price = todayLoaded ? todayData.currentPrice : 0;
        drawClock(timeinfo.tm_hour, timeinfo.tm_min, price);
        return;
    }
    if (!todayLoaded) {
        drawError("Brak danych");
        return;
    }

    // Aktualizuj aktualny przedział cenowy
    int mins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    todayData.currentPeriodIdx = mins / 15;
    if (todayData.currentPeriodIdx >= todayData.totalPeriods)
        todayData.currentPeriodIdx = todayData.totalPeriods - 1;
    if (todayData.currentPeriodIdx >= 0)
        todayData.currentPrice = todayData.prices[todayData.currentPeriodIdx];

    switch (currentScreen) {
        case 0:
            drawToday(todayData, timeinfo.tm_hour, timeinfo.tm_min,
                      TOTAL_SCREENS, yesterdayAvg, (int)currentTrend, alertActive);
            break;
        case 1:
            drawTomorrow(tomorrowData, timeinfo.tm_hour, timeinfo.tm_min,
                         tomorrowLoaded, TOTAL_SCREENS);
            break;
        case 2:
            if (weeklyCount > 0)
                drawWeekly(weeklyAvg, weeklyLabels, weeklyCount,
                           timeinfo.tm_hour, timeinfo.tm_min, TOTAL_SCREENS,
                           weeklyChangePercent, prevWeekAvg, prevWeekCount);
            else
                drawError("Brak historii");
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP32-C3 Energy Price Display ===");

    displayInit();
    touchInit();
    storageInit();

    // ── Załaduj ustawienia użytkownika ──
    UserSettings saved;
    if (storageLoadSettings(saved)) {
        currentBrightness  = saved.brightness;
        manualBrightness   = saved.manualBright;
        screensaverEnabled = saved.screensaverOn;
        priceAlertThreshold = saved.priceAlertThreshold;
        displaySetBrightness(currentBrightness);
        Serial.println("[APP] Settings restored from flash");
    }

    // ── Konfiguracja WiFi ──
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // zmniejsza TX → chroni USB-JTAG

    // 1. Załaduj zapisane przez AP kredencjały (najwyższy priorytet)
    loadSavedCreds();  // dodaje zapisane creds do WiFiMulti (jeśli istnieją)

    // 2. Dodaj hardcoded fallback z config.h (niższy priorytet)
    wifiMulti.addAP(WIFI_SSID1, WIFI_PASS1);
    wifiMulti.addAP(WIFI_SSID2, WIFI_PASS2);

    // 3. Próbuj połączyć (3 próby)
    bool wifiOk = false;
    for (int attempt = 0; attempt < 3 && !wifiOk; attempt++) {
        if (attempt > 0) {
            WiFi.disconnect(true);
            delay(2000);
        }
        wifiOk = connectWiFi();
    }

    if (!wifiOk) {
        // ── Fallback: tryb AP konfiguracyjny ──
        Serial.println("[WIFI] All attempts failed → starting AP config mode");
        startAPMode();
        // setup() kończy się tutaj; loop() obsłuży tryb AP
        return;
    }

    // ── WiFi OK → tryb normalny ──
    appMode = AppMode::NORMAL;

    syncTime();

    // mDNS
    if (MDNS.begin(OTA_HOSTNAME))
        Serial.printf("[mDNS] http://%s.local\n", OTA_HOSTNAME);

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

    // Web dashboard (tryb STA)
    WebContext wctx;
    wctx.today              = &todayData;
    wctx.tomorrow           = &tomorrowData;
    wctx.todayOk            = &todayLoaded;
    wctx.tomorrowOk         = &tomorrowLoaded;
    wctx.brightness         = &currentBrightness;
    wctx.screenSaverOn      = &screenSaverOn;
    wctx.screensaverEnabled = &screensaverEnabled;
    wctx.manualBrightness   = &manualBrightness;
    wctx.onRefresh          = onWebRefresh;
    wctx.onBrightness       = onWebBrightness;
    wctx.onScreensaverToggle = []() {
        screensaverEnabled = !screensaverEnabled;
        if (!screensaverEnabled) {
            // Wyłączono wygaszacz → natychmiast go gasi
            screenSaverOn = false;
            lastDrawTime  = 0;
        } else {
            // Włączono → reset timera od teraz
            lastTouchTime = millis();
        }
        markSettingsDirty();
        Serial.printf("[WEB] Screensaver -> %s\n", screensaverEnabled ? "ON" : "OFF");
    };
    wctx.onAutoBriToggle = []() {
        manualBrightness = !manualBrightness;
        if (!manualBrightness) {
            currentBrightness = 0;  // wymusza natychmiastowe przeliczenie w loop()
            Serial.println("[WEB] Brightness -> AUTO");
        } else {
            Serial.printf("[WEB] Brightness -> MANUAL (%d%%)\n", currentBrightness);
        }
        markSettingsDirty();
    };
    wctx.priceAlertThreshold = &priceAlertThreshold;
    wctx.onAlertThreshold = [](float val) {
        priceAlertThreshold = val;
        markSettingsDirty();
        Serial.printf("[WEB] Alert threshold -> %.0f\n", val);
    };
    wctx.onWiFiSave = nullptr;  // nie potrzeba w STA (obsługa przez /api/wifi-reset)
    webserverInit(wctx);

    // Inicjalna kolekcja danych
    memset(&todayData,    0, sizeof(todayData));
    memset(&tomorrowData, 0, sizeof(tomorrowData));
    fetchAll();
    loadWeeklyData();

    if (!todayLoaded) drawError("API error");

    lastTouchTime = millis();
    Serial.printf("[SYS] Free heap: %d\n", ESP.getFreeHeap());
    Serial.printf("[SYS] Screensaver: %s (default: OFF)\n",
                  screensaverEnabled ? "ON" : "OFF");
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────

void loop() {

    // ── Tryb AP captive portal ────────────────────────────────────────────────
    if (appMode == AppMode::AP_CONFIG) {
        apServerHandle();

        // Co 5 sekund odśwież ekran (pokazuj czas AP mode)
        unsigned long now = millis();
        if (now - lastDrawTime >= 5000) {
            lastDrawTime = now;
            // Pokaż info na ekranie – czas który minął od startu AP
            unsigned long elapsed = (now - apStartTime) / 1000;
            unsigned long remaining = (AP_TIMEOUT_MS / 1000 > elapsed)
                                      ? (AP_TIMEOUT_MS / 1000 - elapsed) : 0;
            Serial.printf("[AP] Active %lus, remaining %lus, clients: %d\n",
                          elapsed, remaining, WiFi.softAPgetStationNum());
            drawAPMode();
        }

        // Timeout AP mode → restart (ale nie gdy ktoś jest podłączony!)
        if (millis() - apStartTime >= AP_TIMEOUT_MS) {
            if (WiFi.softAPgetStationNum() > 0) {
                // Ktoś jest podłączony — reset timera, nie restartuj
                apStartTime = millis();
                Serial.println("[AP] Client connected — timer reset");
            } else {
                Serial.println("[AP] Timeout — restarting");
                delay(200);
                ESP.restart();
            }
        }
        delay(50);
        return;  // nie wykonuj reszty loop() w trybie AP
    }

    // ── Tryb normalny STA ─────────────────────────────────────────────────────

    ArduinoOTA.handle();
    webserverHandle();
    handleTouch();

    unsigned long now = millis();
    struct tm timeinfo;

    if (getLocalTime(&timeinfo)) {
        // Zmiana dnia → przesunięcie jutro → dziś
        if (lastDay >= 0 && timeinfo.tm_mday != lastDay) {
            Serial.println("[APP] Day changed!");
            if (tomorrowLoaded) {
                todayData  = tomorrowData;
                todayLoaded = true;
            }
            tomorrowLoaded = false;
            currentScreen  = 0;
            lastFetchTime  = 0;
            loadWeeklyData();
        }
        lastDay = timeinfo.tm_mday;

        updateBrightness(timeinfo.tm_hour);
    }

    // Odświeżenie z web
    if (webRefreshRequested) {
        webRefreshRequested = false;
        Serial.println("[WEB] Refresh requested");
        fetchAll();
    }

    // Fetch danych co 15 min
    if (now - lastFetchTime >= FETCH_INTERVAL_MS || !todayLoaded) {
        fetchAll();
    }

    // ── Persystencja ustawień (debounce 2s) ─────────────────────────────────
    if (settingsDirty && (now - settingsDirtyTime >= SETTINGS_DEBOUNCE_MS)) {
        settingsDirty = false;
        UserSettings s;
        s.brightness          = currentBrightness;
        s.manualBright        = manualBrightness;
        s.screensaverOn       = screensaverEnabled;
        s.priceAlertThreshold = priceAlertThreshold;
        storageSaveSettings(s);
    }

    // ── Trend + alert ────────────────────────────────────────────────────────
    if (getLocalTime(&timeinfo) && todayLoaded) {
        computePriceTrend(todayData, timeinfo.tm_hour);
        alertActive = (priceAlertThreshold > 0 && todayData.currentPrice > 0
                       && todayData.currentPrice < priceAlertThreshold);
    }

    // ── Logika wygaszacza ─────────────────────────────────────────────────────
    // screensaverEnabled=false (domyślnie) → timer nigdy nie odpala
    // screensaverEnabled=true → odliczamy od lastTouchTime
    if (screensaverEnabled && !screenSaverOn) {
        if (now - lastTouchTime >= SCREENSAVER_TIMEOUT_MS) {
            screenSaverOn = true;
            Serial.println("[UI] Screensaver ON (timeout)");
        }
    }
    // Wygaszacz wyłączony przez dashboard podczas gdy był aktywny
    if (!screensaverEnabled && screenSaverOn) {
        screenSaverOn = false;
        lastDrawTime  = 0;
        Serial.println("[UI] Screensaver OFF (disabled via web)");
    }

    // ── Rysowanie ekranu co 1 sekundę ────────────────────────────────────────
    if (now - lastDrawTime >= DISPLAY_INTERVAL_MS) {
        lastDrawTime = now;
        if (getLocalTime(&timeinfo)) {
            drawCurrentScreen(timeinfo);
        }
    }

    // ── Watchdog sterty ───────────────────────────────────────────────────────
    static unsigned long lastHeapLog = 0;
    static uint32_t minHeapSeen = 999999;
    if (now - lastHeapLog >= 30000) {
        lastHeapLog = now;
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minFree  = ESP.getMinFreeHeap();
        if (freeHeap < minHeapSeen) minHeapSeen = freeHeap;
        Serial.printf("[SYS] Heap: %u B (min ever: %u, session min: %u)\n",
                      freeHeap, minFree, minHeapSeen);
        if (freeHeap < 15000) {
            Serial.println("[SYS] Heap critical — restarting!");
            delay(100);
            ESP.restart();
        }
    }

    delay(100);
}
