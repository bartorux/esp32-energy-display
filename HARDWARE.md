# Hardware: Sunton ESP32-2424S012C

Compact round-display development board from the "CYD" (Cheap Yellow Display) family by Shenzhen Jingcai Intelligent Co. (Sunton).

## Specs

| Component | Details |
|-----------|---------|
| MCU | ESP32-C3-MINI-1U (RISC-V, 160MHz, 320KB RAM) |
| Flash | 4MB (XMC) |
| Display | GC9A01 1.28" IPS 240x240 round |
| Touch | CST816S capacitive (I2C) |
| USB | Built-in USB-Serial/JTAG (no external chip) |
| Size | 38.5 x 37mm |

## Pinout

### Display (SPI)
| Pin | GPIO |
|-----|------|
| MOSI | 7 |
| SCLK | 6 |
| CS | 10 |
| DC | 2 |
| BL | 3 |
| RST | -1 (not connected) |

### Touch (I2C)
| Pin | GPIO |
|-----|------|
| SDA | 4 |
| SCL | 5 |
| INT | 0 |
| RST | 1 |
| Address | 0x15 |

## Battery

- **Connector:** JST-1.25mm 2-pin (single cell 3.7V LiPo)
- **Charging:** Via USB-C, onboard charging IC
- **Protection:** Overcharge + overcurrent
- **Power switch:** SW1 on the side, wired to GPIO8 (10k pull-up)
- **No fuel gauge** — battery % not readable without external sensor

## Links

- [Pinout & specs (espboards.dev)](https://www.espboards.dev/esp32/cyd-esp32-2424s012/)
- [openHASP config](https://www.openhasp.com/0.7.0/hardware/sunton/esp32-2424s012/)
- [CNX Software review](https://www.cnx-software.com/2024/08/06/arduino-and-lvgl-compatible-esp32-c3-board-features-a-1-28-inch-round-touchscreen-display-fully-housed-in-a-case/)

## WiFi

ESP32-C3 supports **2.4 GHz only**. If using a phone hotspot, make sure it broadcasts on 2.4 GHz (not 5 GHz).
