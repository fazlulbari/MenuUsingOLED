#ifndef IMAGES_H
#define IMAGES_H

#include <stdint.h>  // Ensure the type uint8_t is recognized

extern const uint8_t bitmap_logo1[];
extern const uint8_t bitmap_logo2[];
extern const uint8_t bitmap_logo3[];
extern const uint8_t bitmap_logo4[];

extern const uint8_t ICON_WATER_16[];
extern const uint8_t ICON_SMOKE_16[];
extern const uint8_t ICON_DOOR_16[];
extern const uint8_t ICON_FIRE_16[]; 
extern const uint8_t ICON_FAN_16[]; 
extern const uint8_t ICON_LIGHT_16[];

// All are 1-bit XBM (monochrome)
extern const uint8_t ICON_STATUS_24[]    ; // 24x24
extern const uint8_t ICON_ALARMS_24[]    ; // 24x24
extern const uint8_t ICON_SETTINGS_24[]  ; // 24x24
extern const uint8_t ICON_ABOUT_24[]     ; // 24x24

extern const uint8_t ICON_STATUS_16[]    ; // 16x16
extern const uint8_t ICON_ALARMS_16[]    ; // 16x16
extern const uint8_t ICON_SETTINGS_16[]  ; // 16x16
extern const uint8_t ICON_ABOUT_16[]     ; // 16x16
extern const uint8_t ICON_SUM_16[]       ; // 16x16
extern const uint8_t ICON_MOON_16[]      ; // 16x16

extern const uint8_t* images[4];  // declaration only
#endif