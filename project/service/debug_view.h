#ifndef _DEBUG_VIEW_H_
#define _DEBUG_VIEW_H_

/**
 * @brief VOFA+ 上位机调试服务
 * @details 处理来自 VOFA+ 的串口指令，并上报实时的控制数据
 */
void debug_vofa_service(void);

/**
 * @brief 在屏幕上显示 ADC 电感数据
 */
void printf_adc(void);

/**
 * @brief 在屏幕上显示 IMU（加速度/陀螺仪）数据
 */
void printf_imu(void);

/**
 * @brief 在屏幕上显示常规运行状态数据（偏差、速度等）
 */
void printf_date(void);

/**
 * @brief 在屏幕上显示速度环测试相关数据
 */
void printf_speed_test(void);

/**
 * @brief 在屏幕上显示按键测试状态
 */
void printf_butten_test(void);

#endif /* _DEBUG_VIEW_H_ */
