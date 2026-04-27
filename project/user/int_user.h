#ifndef _INT_USER_H_
#define _INT_USER_H_

#include "zf_common_typedef.h"

/**
 * @brief 用户层总初始化核心入口函数
 *
 * @details
 * 建立单片机的外设基本环境工作基准。依次完成：
 * 1. 硬件引脚及通信端口分配 (ADC/PWM/UART)；
 * 2. 控制参数载入与引擎就绪 (EEPROM 加载，PID初始化)；
 * 3. 开启所需的各种定时与外部中断，确立运行主轴。
 *
 * @note 禁止在此处引入不可预知长度的阻塞，此函数执行完毕代表系统转入死循环就绪态。
 */
void int_user(void);

/**
 * @brief 应用最新的全局配置到 PID 控制器
 * @details 将 app 中的参数同步到实时运行的 PID 实例中
 */
void control_apply_config(void);

/**
 * @brief 保存当前配置至 EEPROM
 */
void config_save(void);

/**
 * @brief 从 EEPROM 加载配置
 */
void config_load(void);

#endif /* _INT_USER_H_ */
