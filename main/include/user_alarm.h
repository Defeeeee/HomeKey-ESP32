#ifndef USER_ALARM_H
#define USER_ALARM_H
#ifdef __cplusplus
extern "C" {
#endif
void user_alarm_setup();
void user_alarm_loop();
void user_alarm_disarm();
void user_alarm_arm_away();
void user_alarm_failed_tap();
const char* user_alarm_get_state_string();
bool user_alarm_get_sensor_state(int id);
bool user_alarm_is_zone_bypassed(int zoneIdx);
void user_alarm_set_zone_bypass(int zoneIdx, bool bypassed);
bool user_alarm_is_zone_disabled(int zoneIdx);
#ifdef __cplusplus
}
#endif
#endif
