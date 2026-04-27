#include "motor.h"

/* 全局控制标志位 */
volatile uint8 stop = 0;   /* 停车标志位，1 表示紧急停车保护 */
volatile float dianya = 0; /* 当前电池电压值 */

/**
 * @brief 电机及相关硬件初始化
 * @details 配置 PWM 频率、分辨率及引脚方向
 */
void motor_Init(void)
{
    /*
     * 左电机 PWM 初始化
     * 使用 PWMB 通道 2，引脚 P13，频率 24kHz（避开人耳听觉频率，减小啸叫）
     */
    pwm_init(PWMB_CH2_P13, 17000, 0);
    /* 左电机方向控制引脚 P14，设置为推挽输出 */
    gpio_init(IO_P14, GPO, 0, GPO_PUSH_PULL);
    /*
     * 右电机 PWM 初始化
     * 使用 PWMB 通道 3，引脚 P52，频率 24kHz
     */
    pwm_init(PWMB_CH3_P52, 17000, 0);
    /* 右电机方向控制引脚 P53，设置为推挽输出 */
    gpio_init(IO_P53, GPO, 1, GPO_PUSH_PULL);
}

/**
 * @brief 输出 PWM 限幅
 * @details 将输出限制在 ±MOTOR_OUTPUT_PWM_LIMIT，避免过驱
 */
static int32 motor_limit_output_pwm(int32 pwm)
{
    if (pwm > MOTOR_OUTPUT_PWM_LIMIT)
    {
        return MOTOR_OUTPUT_PWM_LIMIT;
    }
    if (pwm < -MOTOR_OUTPUT_PWM_LIMIT)
    {
        return -MOTOR_OUTPUT_PWM_LIMIT;
    }
    return pwm;
}

/**
 * @brief 电机 PWM 占空比输出控制
 * @details 根据 lpwm 和 rpwm 的正负号控制电机正反转
 * @param lpwm 左轮目标占空比（函数内会限幅到 ±MOTOR_OUTPUT_PWM_LIMIT）
 * @param rpwm 右轮目标占空比（函数内会限幅到 ±MOTOR_OUTPUT_PWM_LIMIT）
 */
void motor_output(int32 lpwm, int32 rpwm)
{
    int32 lpwm_limited;
    int32 rpwm_limited;

    lpwm_limited = motor_limit_output_pwm(lpwm);
    rpwm_limited = motor_limit_output_pwm(rpwm);

    /* 检查停车标志位，stop 为 0 时正常运行 */
    if (stop == 0)
    {
        /* --- 右电机控制逻辑 (硬件映射可能交叉) --- */
        if (lpwm_limited > 0)
        {
            P14 = 1;                                  /* 设置方向：正转 */
            pwm_set_duty(PWMB_CH2_P13, lpwm_limited); /* 设置 PWM 占空比 */
        }
        else if (lpwm_limited < 0)
        {
            P14 = 0;                                   /* 设置方向：反转 */
            pwm_set_duty(PWMB_CH2_P13, -lpwm_limited); /* 取绝对值输出 PWM */
        }
        else
        {
            pwm_set_duty(PWMB_CH2_P13, 0); /* 停止输出 */
        }

        /* --- 左电机控制逻辑 --- */
        if (rpwm_limited > 0)
        {
            P53 = 1; /* 设置方向：正转 */
            pwm_set_duty(PWMB_CH3_P52, rpwm_limited);
        }
        else if (rpwm_limited < 0)
        {
            P53 = 0; /* 设置方向：反转 */
            pwm_set_duty(PWMB_CH3_P52, -rpwm_limited);
        }
        else
        {
            pwm_set_duty(PWMB_CH3_P52, 0); /* 停止输出 */
        }
    }
    else
    {
        /*
         * 保护状态：强制输出极低占空比或直接设为 100
         * 此处 设置成 100，设置成0会出现电机无法完全停止的情况，可能是由于 PWM 输出的非线性或电机特性导致的死区现象
         * 100 的占空比足以让电机保持静止状态，同时也能在某些情况下提供微小的反向力矩来抵消外部扰动，从而更有效地实现停车保护
         */
        pwm_set_duty(PWMB_CH2_P13, 100);
        pwm_set_duty(PWMB_CH3_P52, 100);
    }
}

/**
 * @brief 丢线保护逻辑
 * @details 当四路电感传感器采集值连续多次低于阈值时判定为丢线
 */
void lost_lines(void)
{
    static int8 count = 0; /* 丢线确认计数器 */

    /*
     * ad1~ad4 为全局电感采样值
     * 阈值 3 为根据实际环境标定的最小有效电感强度
     */
    if (ad1 < 3 && ad2 < 3 && ad3 < 3 && ad4 < 3)
    {
        count++;
    }
    else
    {
        count = 0; /* 检测到线，计数器清零 */
    }

    /* 连续 5 次检测到丢线，触发保护停车 */
    if (count > 5)
    {
        count = 0;
        stop = 1;
    }
}

/**
 * @brief 电池电压监测与欠压保护
 * @details 通过 ADC 采样 P05 引脚的分压电压来判断电池剩余电量
 */
void dianya_jiance(void)
{
    static int32 dianya_count = 0; /* 欠压持续计数 */
    uint16 adc_raw;

    /* 执行 ADC 转换 */
    adc_raw = adc_convert(ADC_CH13_P05);
    /* 转换公式：ADC值 * 转换系数（0.0092 需要根据分压电路电阻比例计算） */
    dianya = (float)adc_raw * 0.0092f*4;

    /* 锂电池欠压判定：低于 11.3V（假设为 3S 锂电） */
    if (dianya < 11.2f)
    {
        dianya_count++;
    }
    else
    {
        dianya_count = 0;
    }

    /* 持续欠压 3000 次（软件滤波，防止启动大电流导致电压跌落误判） */
    if (dianya_count > 1000)
    {
        stop = 1; /* 锁定停车，保护电池 */
    }
}

/* --- 电机前馈控制查表数据 --- */
/* 速度测试点（单位：cm/s 或 编码器原始单位） */
static const float ff_speed_points[] = {
    0.0f, 5.4f, 13.0f, 18.4f, 22.2f, 27.6f, 34.2f, 38.0f,
    45.8f, 49.6f, 57.2f, 64.8f, 67.8f, 76.0f, 81.8f, 88.4f, 93.8f};

/* 对应速度点所需的 PWM 占空比 */
static const int16 ff_duty_points[] = {
    0, 500, 1000, 1500, 2000, 2500, 3000, 3500,
    4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000};

/**
 * @brief 速度前馈查表（线性插值）
 * @details 绕过 PID 积分项缓慢累加过程，直接根据目标速度给定基础 PWM 占空比
 * @param speed 目标速度值
 * @return 对应的 PWM 基础占空比
 */
int32 motor_speed_to_duty(float speed)
{
    float s;    /* 速度绝对值 */
    int i;      /* 循环索引 */
    int32 duty; /* 插值计算结果 */

    if (speed > 0.0f)
        s = speed;
    else if (speed < 0.0f)
        s = -speed;
    else
        return 0;

    /* 遍历查找速度所在的区间 */
    for (i = 0; i < 16; i++)
    {
        if (s <= ff_speed_points[i + 1])
        {
            float x0 = ff_speed_points[i];
            float x1 = ff_speed_points[i + 1];
            int32 y0 = (int32)ff_duty_points[i];
            int32 y1 = (int32)ff_duty_points[i + 1];

            /* 线性插值公式：y = y0 + (s - x0) * (y1 - y0) / (x1 - x0) */
            float t = (s - x0) / (x1 - x0);
            duty = (int32)(y0 + t * (float)(y1 - y0));

            /* 恢复速度符号并限幅输出 */
            if (speed < 0.0f)
                duty = -duty;

            if (duty > PWM_DUTY_MAX)
                duty = PWM_DUTY_MAX;
            if (duty < -PWM_DUTY_MAX)
                duty = -PWM_DUTY_MAX;

            return duty;
        }
    }

    /* 速度超过表格最大值，返回最大占空比 */
    duty = (int32)ff_duty_points[16];
    if (speed < 0.0f)
        duty = -duty;
    return duty;
}
