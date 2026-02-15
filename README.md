# ESP32-C3 Energy Price Display

## Hardware

| Parametr | Wartość |
|----------|---------|
| Chip | ESP32-C3 (QFN32, rev 0.4) |
| CPU | RISC-V single-core, 160MHz |
| RAM | 320KB SRAM |
| Flash | 4MB (XMC) |
| WiFi | 2.4GHz 802.11 b/g/n (BEZ 5GHz!) |
| BLE | Bluetooth 5 (LE) |
| USB | USB-Serial/JTAG (wbudowany, bez CH340) |
| MAC | 34:b7:da:2d:5e:6c |
| Ekran | GC9A01 240x240 okragly LCD (SPI) |
| Port | /dev/cu.usbmodem211301 |

## Pinout ekranu

| Pin | GPIO | Opis |
|-----|------|------|
| SCLK | 6 | SPI Clock |
| MOSI | 7 | SPI Data |
| CS | 10 | Chip Select |
| DC | 2 | Data/Command |
| BL | 3 | Backlight |
| RST | -1 | Reset (tied to power) |

## Biblioteki (PlatformIO)

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino

lib_deps =
    bodmer/TFT_eSPI@^2.5.43    ; Sterownik ekranu GC9A01
    bblanchon/ArduinoJson@^7.3.0 ; Parsowanie JSON z API
    ; WiFi, HTTPClient, WiFiClientSecure - wbudowane w framework
```

### Wazne: Bug TFT_eSPI na ESP32-C3 z IDF 5.x

TFT_eSPI wymaga patcha - `REG_SPI_BASE(SPI2_HOST=1)` zwraca 0 zamiast adresu SPI2.
Patch w: `.pio/libdeps/*/TFT_eSPI/Processors/TFT_eSPI_ESP32_C3.h`

```c
// PRZED (oryginalne):
#ifndef REG_SPI_BASE
  #define REG_SPI_BASE(i) DR_REG_SPI2_BASE
#endif

// PO (fix):
#ifdef REG_SPI_BASE
  #undef REG_SPI_BASE
#endif
#define REG_SPI_BASE(i) DR_REG_SPI2_BASE
```

## API PSE (Polskie Sieci Elektroenergetyczne)

- Endpoint: `https://api.raporty.pse.pl/api/rce-pln`
- Filtr: `?$filter=business_date%20eq%20'YYYY-MM-DD'`
- Publiczne, bez autoryzacji
- 96 rekordow/dzien (15-min interwaly)
- Pole `rce_pln` = cena w PLN/MWh
- Dane publikowane day-ahead (~13:30 CET)
- Response: ~25KB JSON

## Komendy

```bash
# Build
pio run

# Upload
pio run -t upload --upload-port /dev/cu.usbmodem211301

# Serial monitor
pio device monitor --port /dev/cu.usbmodem211301 --baud 115200

# Clean build
pio run -t clean
```

## Znane problemy

1. USB-JTAG traci polaczenie podczas WiFi init - brownout detector wylaczony
2. TFT_eSPI wymaga patcha na ESP32-C3 (patrz wyzej)
3. ESP32-C3 NIE obsluguje WiFi 5GHz
