#pragma once

#include "pins.h"

#define IX_NAME "IncognitoX"

#define TFT_SPI_HZ      40000000UL
#define TFT_ROTATION    2
#define TFT_INVERT      1

#define UI_W            240
#define UI_STATUS_H     24
#define UI_HINT_H       24
#define UI_ROW_H        31
#define UI_CONTENT_Y0   UI_STATUS_H
#define UI_CONTENT_Y1   (320 - UI_HINT_H)

#define LOG_LINES       24
#define LOG_MAXLEN      96
#define LOG_DIR         "/logs"
#define WARDIRVE_DIR    "/wardrive"
#define SUBGHZ_DIR      "/subghz"
#define IR_DIR          "/ir"
#define MAPS_DIR        "/maps"

#define MAX_WIFI_APS    48
#define MAX_BLE_DEVS    40
#define MAX_IR_CODES    24
#define MAX_SUBGHZ      32
#define PULSE_MAX_EDGES 400
#define IR_MAX_EDGES    160

#define KEY_DEBOUNCE_MS   18
#define KEY_REPEAT_MS    120
#define KEY_REPEAT_DELAY 380
#define KEY_LONG_MS      700
#define TOAST_MS        1400
