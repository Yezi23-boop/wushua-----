#ifndef _INT_USER_H_
#define _INT_USER_H_

#include "zf_common_typedef.h"

/**
 * @brief 用户层总初始化入口
 * @details 负责调用所有硬件驱动、控制算法及应用逻辑的初始化函数
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
