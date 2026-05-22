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
#ifdef __cplusplus
}
#endif
#endif
