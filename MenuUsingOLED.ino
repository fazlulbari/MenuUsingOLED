#include <Arduino.h>
#include <STM32FreeRTOS.h>     // <-- this library
#include "MenuUI.h"
#include "AppData.h"

// ===== UI task config =====
static TaskHandle_t uiTaskHandle = nullptr;
// Stack depth is in 32-bit WORDS on Cortex-M. 2048 words ≈ 8 KB.
static constexpr uint16_t UI_TASK_STACK_WORDS = 2048;
static constexpr UBaseType_t UI_TASK_PRIORITY  = tskIDLE_PRIORITY + 2;
static constexpr TickType_t  UI_LOOP_DELAY     = pdMS_TO_TICKS(5);

static void uiTask(void*){
  for(;;){
    uiLoop();                    // your existing non-blocking loop
    vTaskDelay(UI_LOOP_DELAY);   // cooperative yield
  }
}

void setup() {
  Serial.begin(115200);
  while(!Serial) { /* optional */ }
  Serial.println("ODCC Menu (STM32 + STM32FreeRTOS) start");

  // Your original init (keep I2C/U8g2 init in setup)
  demoDataInit();
  uiSetup();

  // Create UI task
  BaseType_t ok = xTaskCreate(
    uiTask,                // task function
    "UI",                  // name
    UI_TASK_STACK_WORDS,   // stack (WORDS, not bytes)
    nullptr,               // arg
    UI_TASK_PRIORITY,      // priority
    &uiTaskHandle          // handle
  );
  if (ok != pdPASS) {
    Serial.println("ERROR: UI task create failed (heap/stack too small).");
    for(;;); // halt
  }

  // *** IMPORTANT: start scheduler on STM32FreeRTOS ***
  vTaskStartScheduler();

}

void loop() {
  // Not used: FreeRTOS runs tasks.
}
