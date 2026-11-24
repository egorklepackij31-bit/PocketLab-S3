#pragma once

#ifndef IX_USE_PSRAM
#define IX_USE_PSRAM 0
#endif

#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_MISO 13
#define PIN_TFT_CS   10
#define PIN_TFT_DC    9
#define PIN_TFT_RST   2
#define PIN_TFT_BL    8

#define PIN_SD_CS      5

#define PIN_CC_CS      6
#define PIN_CC_GDO0    7
#define PIN_CC_GDO2   16

#define PIN_RC522_CS   21
#define PIN_RC522_RST  42

#define PIN_NRF_CS     18
#define PIN_NRF_CE     46

#define PIN_I2C_SDA    40
#define PIN_I2C_SCL    41
#define PN532_I2C_ADDR 0x24

#define PIN_IR_RX      1
#define PIN_IR_TX     15

#define PIN_BUZZER     4

#define PIN_BATTERY_ADC 3
#define BATTERY_DIVIDER 2.0f

#define PIN_GPS_RX     17
#define PIN_GPS_TX     47
#define GPS_BAUD       9600

#if IX_USE_PSRAM

  #define PIN_BTN_UP     38
  #define PIN_BTN_DOWN   39
  #define PIN_BTN_LEFT    0
  #define PIN_BTN_RIGHT  14
  #define PIN_BTN_OK     48
#else
  #define PIN_BTN_UP     35
  #define PIN_BTN_DOWN   36
  #define PIN_BTN_LEFT   37
  #define PIN_BTN_RIGHT  38
  #define PIN_BTN_OK     39
#endif
