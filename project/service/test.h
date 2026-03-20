#ifndef _TEST_H_
#define _TEST_H_

/**
 * @brief 速度环闭环测试函数
 * @details 用于在空车或特定环境下测试左右轮的速度跟随情况
 */
void test_speed_func(void);

/**
 * @brief 角度环闭环测试函数
 * @details 测试姿态控制器对车身角度偏差的响应
 */
void test_angle_func(void);

/* --- 测试目标值全局变量 --- */
extern float test_speed_value; /**< 测试用的目标速度 */
extern float test_angle_value; /**< 测试用的目标角度 */

#endif /* _TEST_H_ */
