#pragma once
// Fix REG_SPI_BASE for ESP32-C3 with Arduino ESP32 core 3.x (IDF 5.x)
// Problem: SPI2_HOST=1 but REG_SPI_BASE(1) returns 0x0 instead of 0x60024000
// ESP32-C3 has only one general-purpose SPI (SPI2) at 0x60024000
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ESP32C3)
  #ifdef REG_SPI_BASE
    #undef REG_SPI_BASE
  #endif
  #define REG_SPI_BASE(i) (0x60024000)
#endif
