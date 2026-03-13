#ifndef __MOTOR_H__
#define __MOTOR_H__
#include "zf_common_headfile.h"

void motor_Init();
void motor_output(int32 lpwm, int32 rpwm);
void lost_lines();
extern uint8 stop;
extern float dianya; 
void dianya_jiance();
int32 motor_speed_to_duty(float speed);
#endif
