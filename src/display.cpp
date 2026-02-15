#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <math.h>

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);
static bool spriteOk = false;

// Animated gradient state
static float animPhase = 0.0f;       // breathing phase 0..2*PI
static float smoothT = 0.5f;         // smoothed price-t (for transitions)
static unsigned long lastAnimTime = 0;

// White text shades
#define C_WHITE       0xFFFF
#define C_WHITE85     0xD69A
#define C_WHITE60     0x9CF3
#define C_WHITE40     0x6B4D

// ---- Price → background color: green → amber → red ----

static uint16_t priceBg(float t) {
    t = constrain(t, 0.0f, 1.0f);
    uint8_t r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;
        r = 10 + s * 85;     // 10 → 95
        g = 78 - s * 13;     // 78 → 65
        b = 50 - s * 42;     // 50 → 8
    } else {
        float s = (t - 0.5f) * 2.0f;
        r = 95 + s * 20;     // 95 → 115
        g = 65 - s * 50;     // 65 → 15
        b = 8 + s * 7;       // 8 → 15
    }
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static float priceT(float price, float minP, float maxP) {
    if (maxP <= minP) return 0.5f;
    return constrain((price - minP) / (maxP - minP), 0.0f, 1.0f);
}

// ---- Helpers ----

static void updateAnim(float targetT) {
    unsigned long now = millis();
    float dt = (now - lastAnimTime) / 1000.0f;
    lastAnimTime = now;
    if (dt > 0.5f) dt = 0.5f;

    smoothT += (targetT - smoothT) * constrain(dt * 0.8f, 0.0f, 1.0f);

    animPhase += dt * 0.78f;
    if (animPhase > 6.2832f) animPhase -= 6.2832f;
}

static void drawBg(float t) {
    updateAnim(t);

    // Triangle wave breathing +-6%
    float wave = animPhase / 3.14159f;
    if (wave > 1.0f) wave = 2.0f - wave;
    wave = wave * 2.0f - 1.0f;  // -1..1

    float at = constrain(smoothT + wave * 0.06f, 0.0f, 1.0f);

    uint16_t bg = priceBg(at);
    float edgeMul = 0.52f + wave * 0.02f;
    uint8_t er, eg, eb;
    if (at < 0.5f) {
        float s = at * 2.0f;
        er = (10 + s * 85) * edgeMul;
        eg = (78 - s * 13) * edgeMul;
        eb = (50 - s * 42) * edgeMul;
    } else {
        float s = (at - 0.5f) * 2.0f;
        er = (95 + s * 20) * edgeMul;
        eg = (65 - s * 50) * edgeMul;
        eb = (8 + s * 7) * edgeMul;
    }
    uint16_t edge = ((er >> 3) << 11) | ((eg >> 2) << 5) | (eb >> 3);

    spr.fillSprite(TFT_BLACK);
    spr.fillCircle(CENTER_X, CENTER_Y, 119, edge);
    spr.fillCircle(CENTER_X, CENTER_Y, 113, bg);
}

static void drawDots(int current, int total) {
    if (total < 2) return;
    int y = 222, sp = 12;
    int sx = CENTER_X - (total - 1) * sp / 2;
    for (int i = 0; i < total; i++) {
        int x = sx + i * sp;
        if (i == current) spr.fillCircle(x, y, 3, C_WHITE);
        else spr.drawCircle(x, y, 3, C_WHITE60);
    }
}

static void drawBarChart(const PriceData &d, int hlIdx) {
    if (d.totalPeriods == 0) return;
    const int cx = 42, cw = 156, cy = 152, ch = 50;
    int hlH = hlIdx >= 0 ? hlIdx / 4 : -1;

    // Bottom line
    spr.drawFastHLine(cx, cy + ch, cw, C_WHITE40);

    for (int h = 0; h < 24; h++) {
        if (d.hourlyAvg[h] <= 0) continue;
        float norm = 0.05f;
        if (d.maxPrice > d.minPrice)
            norm = constrain((d.hourlyAvg[h] - d.minPrice) / (d.maxPrice - d.minPrice), 0.05f, 1.0f);
        int bx = cx + h * cw / 24;
        int bx2 = cx + (h + 1) * cw / 24;
        int bw = bx2 - bx - 1;
        if (bw < 1) bw = 1;
        int bh = (int)(norm * (ch - 4));
        int by = cy + ch - bh;
        spr.fillRect(bx, by, bw, bh, (h == hlH) ? C_WHITE : C_WHITE60);
    }

    // Hour labels
    spr.setTextColor(C_WHITE40);
    spr.setTextFont(1);
    spr.setTextDatum(TL_DATUM);
    spr.drawString("0", cx, cy + ch + 2);
    spr.setTextDatum(TC_DATUM);
    spr.drawString("6", cx + cw / 4, cy + ch + 2);
    spr.drawString("12", cx + cw / 2, cy + ch + 2);
    spr.drawString("18", cx + 3 * cw / 4, cy + ch + 2);
}

static void drawStats3(float v1, float v2, float v3,
                       const char *l1, const char *l2, const char *l3) {
    spr.setTextDatum(TC_DATUM);

    // Labels
    spr.setTextFont(1);
    spr.setTextColor(C_WHITE60);
    spr.drawString(l1, 62, 127);
    spr.drawString(l2, CENTER_X, 127);
    spr.drawString(l3, 178, 127);

    // Values
    char tmp[10];
    spr.setTextFont(2);
    spr.setTextColor(C_WHITE);
    snprintf(tmp, sizeof(tmp), "%.0f", v1);
    spr.drawString(tmp, 62, 137);
    snprintf(tmp, sizeof(tmp), "%.0f", v2);
    spr.drawString(tmp, CENTER_X, 137);
    snprintf(tmp, sizeof(tmp), "%.0f", v3);
    spr.drawString(tmp, 178, 137);
}

// ============ Public API ============

void displayInit() {
    Serial.println("[DISP] Init...");
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // PWM backlight (channel 0)
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, 255);

    void *p = spr.createSprite(SCREEN_SIZE, SCREEN_SIZE);
    if (p) {
        spriteOk = true;
        spr.setSwapBytes(true);
        Serial.println("[DISP] Sprite OK");
    } else {
        spriteOk = false;
        Serial.println("[DISP] Sprite FAILED");
    }
    Serial.println("[DISP] Ready");
}

void displaySetBrightness(uint8_t percent) {
    uint8_t duty = (uint8_t)((uint16_t)percent * 255 / 100);
    ledcWrite(0, duty);  // channel 0
}

void drawToday(const PriceData &data, int hour, int minute, int totalScreens,
               float yesterdayAvg) {
    if (!spriteOk) return;

    float t = priceT(data.currentPrice, data.minPrice, data.maxPrice);
    drawBg(t);

    // Time
    struct tm ti;
    int sec = 0;
    if (getLocalTime(&ti)) sec = ti.tm_sec;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, sec);
    spr.setTextColor(C_WHITE85);
    spr.setTextDatum(TC_DATUM);
    spr.setTextFont(2);
    spr.drawString(buf, CENTER_X, 18);

    // Label
    spr.setTextColor(C_WHITE60);
    spr.setTextFont(1);
    spr.drawString("CENA ENERGII", CENTER_X, 38);

    // Main price
    char pBuf[10];
    if (data.currentPrice >= 1000)
        snprintf(pBuf, sizeof(pBuf), "%.0f", data.currentPrice);
    else
        snprintf(pBuf, sizeof(pBuf), "%.1f", data.currentPrice);

    spr.setTextColor(C_WHITE);
    spr.setTextDatum(TC_DATUM);
    spr.setTextFont(7);
    spr.drawString(pBuf, CENTER_X, 52);

    // Unit
    spr.setTextColor(C_WHITE85);
    spr.setTextFont(2);
    spr.setTextDatum(TC_DATUM);
    spr.drawString("PLN/MWh", CENTER_X, 102);

    // Separator
    spr.drawFastHLine(48, 122, 144, C_WHITE40);

    // Stats
    drawStats3(data.minPrice, data.avgPrice, data.maxPrice, "MIN", "AVG", "MAX");

    // Chart
    drawBarChart(data, data.currentPeriodIdx);

    // Yesterday comparison
    if (yesterdayAvg > 0 && data.avgPrice > 0) {
        float diff = ((data.avgPrice - yesterdayAvg) / yesterdayAvg) * 100.0f;
        char cmp[16];
        if (diff > 0)
            snprintf(cmp, sizeof(cmp), "\x18%.0f%%", diff);   // ↑
        else
            snprintf(cmp, sizeof(cmp), "\x19%.0f%%", -diff);  // ↓
        spr.setTextDatum(TC_DATUM);
        spr.setTextFont(2);
        spr.setTextColor(diff > 0 ? spr.color565(255, 100, 100)
                                  : spr.color565(100, 255, 160));
        spr.drawString(cmp, CENTER_X, 210);
    }

    // Page dots
    drawDots(0, totalScreens);

    spr.pushSprite(0, 0);
}

void drawTomorrow(const PriceData &data, int hour, int minute, bool available, int totalScreens) {
    if (!spriteOk) return;

    if (!available || data.totalPeriods == 0) {
        drawBg(0.3f);  // neutral greenish

        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        spr.setTextColor(C_WHITE85);
        spr.setTextDatum(TC_DATUM);
        spr.setTextFont(2);
        spr.drawString(buf, CENTER_X, 18);

        spr.setTextColor(C_WHITE);
        spr.setTextFont(4);
        spr.setTextDatum(MC_DATUM);
        spr.drawString("Jutro", CENTER_X, CENTER_Y - 18);

        spr.setTextColor(C_WHITE60);
        spr.setTextFont(2);
        spr.drawString("Brak danych", CENTER_X, CENTER_Y + 10);
        spr.setTextFont(1);
        spr.drawString("dane ok. 13:30", CENTER_X, CENTER_Y + 30);

        drawDots(1, totalScreens);
        spr.pushSprite(0, 0);
        return;
    }

    float t = priceT(data.avgPrice, data.minPrice, data.maxPrice);
    drawBg(t);

    char buf[32];

    // Time
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    spr.setTextColor(C_WHITE85);
    spr.setTextDatum(TC_DATUM);
    spr.setTextFont(2);
    spr.drawString(buf, CENTER_X, 18);

    // Label + cheapest hour
    snprintf(buf, sizeof(buf), "JUTRO / tanio %d:00", data.cheapestHour);
    spr.setTextColor(C_WHITE60);
    spr.setTextFont(1);
    spr.drawString(buf, CENTER_X, 38);

    // Average price
    snprintf(buf, sizeof(buf), "%.0f", data.avgPrice);
    spr.setTextColor(C_WHITE);
    spr.setTextFont(7);
    spr.drawString(buf, CENTER_X, 52);

    // Unit
    spr.setTextColor(C_WHITE85);
    spr.setTextFont(2);
    spr.drawString("PLN/MWh avg", CENTER_X, 102);

    // Separator
    spr.drawFastHLine(48, 122, 144, C_WHITE40);

    // Stats
    drawStats3(data.minPrice, data.avgPrice, data.maxPrice, "MIN", "AVG", "MAX");

    // Chart
    drawBarChart(data, -1);

    // Page dots
    drawDots(1, totalScreens);

    spr.pushSprite(0, 0);
}

void drawWeekly(const float *dailyAvg, const char labels[][6], int count,
                int hour, int minute, int totalScreens) {
    if (!spriteOk || count == 0) return;

    // Find min/max for scaling
    float mn = 9999, mx = 0;
    for (int i = 0; i < count; i++) {
        if (dailyAvg[i] < mn) mn = dailyAvg[i];
        if (dailyAvg[i] > mx) mx = dailyAvg[i];
    }

    // Use overall avg for background color
    float sum = 0;
    for (int i = 0; i < count; i++) sum += dailyAvg[i];
    float avg = sum / count;
    float t = priceT(avg, PRICE_LOW, PRICE_HIGH);
    drawBg(t);

    // Time
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    spr.setTextColor(C_WHITE85);
    spr.setTextDatum(TC_DATUM);
    spr.setTextFont(2);
    spr.drawString(buf, CENTER_X, 18);

    // Label
    spr.setTextColor(C_WHITE60);
    spr.setTextFont(1);
    snprintf(buf, sizeof(buf), "TYDZIEN / avg %.0f", avg);
    spr.drawString(buf, CENTER_X, 38);

    // Bar chart area
    const int cx = 32, cy = 58;
    const int cw = 176, ch = 120;
    float range = mx - mn;
    if (range < 1) range = 1;

    int barW = (cw - (count - 1) * 2) / count;
    if (barW < 4) barW = 4;
    int totalW = count * barW + (count - 1) * 2;
    int startX = cx + (cw - totalW) / 2;

    // Bottom line
    spr.drawFastHLine(cx, cy + ch, cw, C_WHITE40);

    // Bars
    int minIdx = 0, maxIdx = 0;
    for (int i = 0; i < count; i++) {
        if (dailyAvg[i] <= dailyAvg[minIdx]) minIdx = i;
        if (dailyAvg[i] >= dailyAvg[maxIdx]) maxIdx = i;
    }

    for (int i = 0; i < count; i++) {
        float norm = constrain((dailyAvg[i] - mn) / range, 0.05f, 1.0f);
        int bh = (int)(norm * (ch - 10));
        int bx = startX + i * (barW + 2);
        int by = cy + ch - bh;

        // Color: green for cheapest, red for most expensive, white for rest
        uint16_t col;
        if (i == minIdx)
            col = spr.color565(52, 211, 153);   // green
        else if (i == maxIdx)
            col = spr.color565(248, 113, 113);   // red
        else if (i == count - 1)
            col = C_WHITE;                        // today = bright
        else
            col = C_WHITE60;

        spr.fillRect(bx, by, barW, bh, col);

        // Value on top of bar
        snprintf(buf, sizeof(buf), "%.0f", dailyAvg[i]);
        spr.setTextDatum(BC_DATUM);
        spr.setTextFont(1);
        spr.setTextColor(col);
        spr.drawString(buf, bx + barW / 2, by - 2);

        // Date label below
        spr.setTextDatum(TC_DATUM);
        spr.setTextColor(C_WHITE40);
        spr.drawString(labels[i], bx + barW / 2, cy + ch + 3);
    }

    // Page dots
    drawDots(2, totalScreens);

    spr.pushSprite(0, 0);
}

void drawConnecting() {
    if (!spriteOk) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.drawString("Laczenie...", CENTER_X, CENTER_Y);
        return;
    }
    spr.fillSprite(TFT_BLACK);
    spr.fillCircle(CENTER_X, CENTER_Y, 119, spr.color565(12, 40, 55));
    spr.setTextColor(C_WHITE);
    spr.setTextDatum(MC_DATUM);
    spr.setTextFont(4);
    spr.drawString("Laczenie...", CENTER_X, CENTER_Y);
    spr.pushSprite(0, 0);
}

void drawError(const char *msg) {
    if (!spriteOk) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.drawString(msg, CENTER_X, CENTER_Y);
        return;
    }
    spr.fillSprite(TFT_BLACK);
    spr.fillCircle(CENTER_X, CENTER_Y, 119, spr.color565(85, 15, 15));
    spr.setTextColor(C_WHITE);
    spr.setTextDatum(MC_DATUM);
    spr.setTextFont(4);
    spr.drawString(msg, CENTER_X, CENTER_Y);
    spr.pushSprite(0, 0);
}

void drawClock(int hour, int minute, float currentPrice) {
    if (!spriteOk) return;

    // Update animation phase
    unsigned long now = millis();
    float dt = (now - lastAnimTime) / 1000.0f;
    lastAnimTime = now;
    if (dt > 0.5f) dt = 0.5f;
    animPhase += dt * 0.78f;
    if (animPhase > 6.2832f) animPhase -= 6.2832f;

    // Breathing glow ring
    float wave = animPhase / 3.14159f;
    if (wave > 1.0f) wave = 2.0f - wave;
    float glow = 0.3f + (wave * 2.0f - 1.0f) * 0.15f;
    uint8_t gr = 8 + glow * 25;
    uint8_t gg = 18 + glow * 40;
    uint8_t gb = 30 + glow * 55;

    spr.fillSprite(TFT_BLACK);
    spr.fillCircle(CENTER_X, CENTER_Y, 119, spr.color565(gr, gg, gb));
    spr.fillCircle(CENTER_X, CENTER_Y, 113, spr.color565(5, 12, 22));

    // Large time HH:MM
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    spr.setTextColor(C_WHITE);
    spr.setTextDatum(MC_DATUM);
    spr.setTextFont(7);
    spr.drawString(buf, CENTER_X, CENTER_Y - 10);

    // Small price below
    if (currentPrice > 0) {
        snprintf(buf, sizeof(buf), "%.0f", currentPrice);
        spr.setTextFont(2);
        spr.setTextColor(C_WHITE40);
        spr.drawString(buf, CENTER_X - 10, CENTER_Y + 40);
        spr.setTextFont(1);
        spr.drawString("PLN", CENTER_X + 25, CENTER_Y + 43);
    }

    spr.pushSprite(0, 0);
}
