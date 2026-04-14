#ifndef _TEST_H_
#define _TEST_H_

/**
 * @brief 速度环闭环测试函数
 * @details 用于在空车或特定环境下测试左右轮的速度跟随情况
 */
void test_speed_func(void);

/**
 * @brief 直接差速测试函数
 * @details 测试固定差速目标下左右轮与 gyro 采样链路的响应
 */
void test_diff_func(void);

/* --- 测试目标值全局变量 --- */
extern float test_speed_value; /**< 测试用的目标速度 */
extern float test_diff_value;  /**< 测试用的直接差速目标 */

#endif /* _TEST_H_ */
