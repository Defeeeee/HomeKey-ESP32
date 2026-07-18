#ifndef ESP32_TIMER_COMPAT_H
#define ESP32_TIMER_COMPAT_H

#include <Arduino.h>

#if defined(ESP32)
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 3

inline hw_timer_t* timerBegin_compat(uint8_t num, uint16_t prescaler, bool countUp) {
    return timerBegin(80000000 / prescaler);
}
#define timerBegin timerBegin_compat

inline void timerAttachInterrupt_compat(hw_timer_t* timer, void (*userFunc)(void), bool edge) {
    timerAttachInterrupt(timer, userFunc);
}
#define timerAttachInterrupt timerAttachInterrupt_compat

inline void timerAlarmWrite_compat(hw_timer_t* timer, uint64_t alarm_value, bool autoreload) {
    timerAlarm(timer, alarm_value, autoreload, 0);
}
#define timerAlarmWrite timerAlarmWrite_compat

inline void timerAlarmEnable_compat(hw_timer_t* timer) {
    timerStart(timer);
}
#define timerAlarmEnable timerAlarmEnable_compat

inline void timerAlarmDisable_compat(hw_timer_t* timer) {
    timerStop(timer);
}
#define timerAlarmDisable timerAlarmDisable_compat

#endif
#endif

#endif // ESP32_TIMER_COMPAT_H
