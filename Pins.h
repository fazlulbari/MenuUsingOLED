#pragma once
#include <Arduino.h>

// I2C OLED (SSD1306 128x64)
// #define RESET_PIN PB8
// #define I2C_SDA   PB7
// #define I2C_SCL   PB6

// // Buttons (active-LOW, use INPUT_PULLUP)
// #define BTN_UP       PC15
// #define BTN_DOWN     PB5
// #define BTN_ENTER    PC14
// #define BTN_ESC      PC13

// I2C OLED (SSD1306 128x64)
#define RESET_PIN U8X8_PIN_NONE
#define I2C_SDA   PB9
#define I2C_SCL   PB8

// Buttons (active-LOW, use INPUT_PULLUP)
#define BTN_UP       PC15
#define BTN_DOWN     PA0
#define BTN_ENTER    PC14
#define BTN_ESC      PC13

// OLED I2C address
#define OLED_ADDR 0x3C
