#ifndef __EEPROM_H
#define __EEPROM_H

/**
 * EEPROM 参数声明
 */
extern uint8 date_buff[200]; // EEPROM 数据缓存

/**
 * 运行状态标志
 */
extern int16 start_flag;   // 启动标志
extern int16 circle_flags; // 圆环标志

/**
 * 方向环 PID（误差与陀螺分离）
 */
extern float limiting_Err; // 陀螺微分系数
extern float kp_Err;       // 误差比例系数
extern float fuya_xili;    // 负压吸力系数
extern float kd_Err;       // 误差微分系数
extern float kp2_Err;      // PWM 滤波系数
extern float speed_run;    // 运行速度目标

/**
 * 角度环 PID
 */
extern float A_1;            // 角度环系数 A_1
extern float B_1;            // 角度环系数 B_1
extern float C_l;            // 角度环系数 C_l
extern float kp_Angle;       // 角度比例系数
extern float kd_Angle;       // 角度微分系数
extern float limiting_Angle; // 角度限幅

/**
 * 圆环/菱形等赛道相关参数
 */
extern float ring_encoder;
extern float pre_ring_Gyro_set;
extern float in_ring_Gyroz;
extern float pre_out_ring_Gyro_set;
extern float pre_out_ring_Gyroz;
extern float pre_out_ring_encoder;

/**
 * EEPROM 初始化与刷新
 */
void eeprom_init();  // EEPROM 初始化
void eeprom_flash(); // 将当前参数写入 EEPROM

/**
 * EEPROM 读写接口
 */
void save_int(int32 input, uint8 value_bit); // 保存 int 值到指定地址位
int32 read_int(uint8 value_bit);             // 读取指定地址位的 int 值

void save_float(float input, uint8 value_bit); // 保存 float 值到指定地址位
float read_float(uint8 value_bit);             // 读取指定地址位的 float 值
extern int count_fly_speed;
extern int count_fly_time_1;
extern int count_fly_time_2;
extern int count_fly_angle;

#endif
