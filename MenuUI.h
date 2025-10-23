#pragma once
#include <Arduino.h>

// Simple UI states
enum UIMode { UI_IDLE, UI_MENU, UI_SUBMENU };

// Call once in setup()
void uiSetup();

// Call every loop()
void uiLoop();
