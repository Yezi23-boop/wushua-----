/**
 * @file FUYA.c
 * @brief 负压风机固定输出控制
 * @details
 * 负压只保留固定百分比输出链路：
 * 1) 上层写入 0~100%；
 * 2) 本模块映射为 50Hz 电调脉宽 500~1000；
 * 3) 停止态写回最小脉宽，避免待机误拉负压。
 */
#include "zf_common_headfile.h"
#include "FUYA.h"
// 计算无刷电调转速   （1ms - 2ms）/20ms * 10000（10000是PWM的满占空比时候的值）
// 在50Hz的控制频率下，无刷电调转速 0%   为 500
// 在50Hz的控制频率下，无刷电调转速 20%  为 600
// 在50Hz的控制频率下，无刷电调转速 40%  为 700
// 在50Hz的控制频率下，无刷电调转速 60%  为 800
// 在50Hz的控制频率下，无刷电调转速 80%  为 900
// 在50Hz的控制频率下，无刷电调转速 100% 为 1000

// 电调支持50hz-300hz的控制频率
// 50Hz的控制频率 ，从0%到100%占空比为500到1000
// 100Hz的控制频率，从0%到100%占空比为1000到2000
// 200Hz的控制频率，从0%到100%占空比为2000到4000
// 300Hz的控制频率，从0%到100%占空比为3000到6000
/* --- 负压输出量纲定义 --- */
#define FUYA_PERCENT_MIN 0
#define FUYA_PERCENT_MAX 100
#define FUYA_PWM_MIN 500
#define FUYA_PWM_MAX 1000

volatile int fuya_output_pwm = FUYA_PWM_MIN;       /* 当前固定负压输出脉宽 */
volatile uint8 fuya_output_percent = 0;            /* 当前目标百分比 */

/**
 * @brief 限制负压百分比到电调映射允许范围。
 * @param percent 上层菜单或配置写入的百分比。
 * @return uint8 已限制到 0~100 的百分比。
 */
static float fuya_limit_percent(float percent)
{
    /* 上层可能来自菜单、EEPROM 或调试命令，先按 float 限幅再转 8 位，避免提前回绕。 */
    if (percent < (float)FUYA_PERCENT_MIN)
    {
        percent = FUYA_PERCENT_MIN;
    }
    else if (percent > (float)FUYA_PERCENT_MAX)
    {
        percent = FUYA_PERCENT_MAX;
    }
    return (uint8)percent;
}

/**
 * @brief 负压百分比映射到 PWM 脉宽
 * @param percent 0~100 的目标负压百分比
 * @return 对应 PWM 脉宽，范围 FUYA_PWM_MIN~FUYA_PWM_MAX
 */
static float fuya_percent_to_pwm(float percent)
{
    return (float)(FUYA_PWM_MIN + ((float)percent * (FUYA_PWM_MAX - FUYA_PWM_MIN)) / FUYA_PERCENT_MAX);
}

/**
 * @brief 负压模块初始化
 * @details 初始化 PWM 通道并保持 ESC 最小脉宽，等待启停状态机进入预启动后再拉负压。
 */
void fuya_init(void)
{
    pwm_init(PWMA_CH2N_P03, 50, FUYA_PWM_MIN);

    /*
     * 上电不能直接拉负压，避免还在菜单/调参阶段时风机误启动。
     * 固定输出由 run_time_2() 中的预启动状态统一触发。
     */
    fuya_stop();
}

/**
 * @brief 负压输出底层接口
 * @details 对 PWM 做最终限幅，避免越界写入。
 */
void fuya_set_pwm(int pwm)
{
    if (pwm < FUYA_PWM_MIN)
    {
        pwm = FUYA_PWM_MIN;
    }
    else if (pwm > FUYA_PWM_MAX)
    {
        pwm = FUYA_PWM_MAX;
    }

    fuya_output_pwm = pwm;
    pwm_set_duty(PWMA_CH2N_P03, (uint32)pwm);
}

/**
 * @brief 直接设置负压目标百分比
 * @param percent 上层百分比，允许传入 float 配置值，函数内部统一限制到 0~100。
 * @details 该接口用于预启动固定值、菜单或调参命令写入；写入后立即输出，不再按姿态动态更新。
 */
void fuya_set_percent(float percent)
{
    float limited_percent;
    float pwm_value;

    /* 固定负压不再依赖周期调用，必须一次写到目标 PWM。 */
    limited_percent = fuya_limit_percent(percent);
    pwm_value = fuya_percent_to_pwm(limited_percent);

    fuya_output_percent = limited_percent;
    fuya_set_pwm(pwm_value);
}

/**
 * @brief 强制关闭负压
 * @details 用于停止态或保护态，确保风机输出回到最小占空。
 */
void fuya_stop(void)
{
    fuya_output_percent = 0;
    fuya_set_pwm(FUYA_PWM_MIN);
}
