#ifndef USER_ALARM_H
#define USER_ALARM_H
#ifdef __cplusplus
extern "C" {
#endif
void user_alarm_setup();
void user_alarm_loop();
void user_alarm_disarm();
void user_alarm_set_siren_disabled(bool disabled);
bool user_alarm_is_siren_disabled();
unsigned long user_alarm_mqtt_down_ms();
void user_alarm_arm_away();
void user_alarm_arm_home();
void user_alarm_failed_tap();
const char* user_alarm_get_state_string();
bool user_alarm_get_sensor_state(int id);
bool user_alarm_is_zone_bypassed(int zoneIdx);
void user_alarm_set_zone_bypass(int zoneIdx, bool bypassed);
bool user_alarm_is_zone_disabled(int zoneIdx);
void user_alarm_set_sensor_state(int id, bool isOpen);
void user_alarm_siren_test(bool active);
bool user_alarm_is_siren_testing();
#ifdef __cplusplus
}
#endif
#endif
