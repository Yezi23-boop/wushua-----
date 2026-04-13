#include "zf_common_headfile.h"
#include "int_user.h"
#include "../service/key.h"
#include "../service/menu.h"
#include "../service/speed_loop_autotune_adapter.h"

/* 定时器中断周期定义（单位：ms） */
#define TIME_0 2  /* 主控控制环周期 */
#define TIME_1 10 /* 按键与菜单服务周期 */

/* 内部私有初始化函数声明 */
static void hardware_init(void);
static void control_init(void);
static void app_init(void);
static void clamp_steer_output(PID_Steer *pid);
static float clamp_config_percent(float value);
static void timer1_service_10ms(void);

/**
 * @brief 系统初始化函数
 */
void int_user(void)
{
    //	gpio_init(IO_P36, GPO, 1, GPO_PUSH_PULL);
    hardware_init(); /* 1. 硬件平台初始化 */
    control_init();  /* 2. 控制算法参数初始化 */
    app_init();      /* 3. 应用逻辑初始化 */
                     /* 定时器 PIT 初始化 */
    pit_ms_init(TIM0_PIT, TIME_0);
    pit_ms_init(TIM1_PIT, TIME_1);
}

/**
 * @brief 硬件平台与底层驱动初始化
 */
static void hardware_init(void)
{
    /* 初始化系统时钟 */
    system_delay_init();
    ips114_init();                             /* IPS 屏幕 */
    imu660rc_init(IMU660RC_QUARTERNION_240HZ); /* 六轴惯导初始化 */
    eeprom_init();                             /* 参数存储模块 */

    tim1_irq_handler = timer1_service_10ms;

    /* 编码器接口初始化 */
    encoder_dir_init(TIM3_ENCOEDER, IO_P46, TIM3_ENCOEDER_P04);
    encoder_dir_init(TIM4_ENCOEDER, IO_P42, TIM4_ENCOEDER_P06);

    /* ADC 通道初始化 */
    adc_init(ADC_CH13_P05, ADC_8BIT); /* 电池电压采样 */
    adc_init(ADC_CH0_P10, ADC_12BIT); /* 电感 1 */
    adc_init(ADC_CH1_P11, ADC_12BIT); /* 电感 2 */
    adc_init(ADC_CH8_P00, ADC_12BIT); /* 电感 3 */
    adc_init(ADC_CH9_P01, ADC_12BIT); /* 电感 4 */

    /* 应用层模块 */
    motor_Init();         /* 电机驱动 PWM 输出 */
    fuya_Init();          /* 负压风扇 PWM */
    wireless_uart_init(); /* 无线串口（用于调试/上位机） */
}

static void timer1_service_10ms(void)
{
    if (!Menu_Is_Service_Enabled())
        return;

    Keystroke_Scan_10ms();
    Menu_Tick_10ms();
}

/**
 * @brief 控制参数与 PID 实例初始化
 */
static void control_init(void)
{
    /* 速度环初始化，默认提供一组安全基础参数 */
    pid_speed_init(&PID.left_speed, 105.0f, 20.0f, 0.0f, 10000.0f, 10000.0f);
    pid_speed_init(&PID.right_speed, 105.0f, 20.0f, 0.0f, 10000.0f, 10000.0f);

    /* 转向和角度环先清零，具体参数由 apply_config 从 EEPROM 同步 */
    pid_steer_init(&PID.steer, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    pid_steer_init(&PID.angle, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* 同步 EEPROM 参数 */
    control_apply_config();
    speed_loop_autotune_project_init();
}

/**
 * @brief 应用层启动逻辑
 */
static void app_init(void)
{
    /* 上电时在静止状态下采集 gyro_z 零偏，后续控制环自动去零飘。 */
    imu_calibrate_gyro_z_zero_drift();
}

/**
 * @brief 控制参数同步函数
 * @details 把全局配置结构 app 中的值写入 PID 运行实例
 */
void control_apply_config(void)
{
    app.start.fuya_xili = clamp_config_percent(app.start.fuya_xili);
    app.start.fuya_wall_percent = clamp_config_percent(app.start.fuya_wall_percent);

    /* 1. 同步转向环，包含二次校正项 */
    PID.steer.Kp = app.speed.kp_Err;
    PID.steer.Kd = app.speed.kd_Err;
    PID.steer.Kp2 = app.speed.kp2_Err;
    PID.steer.max_output = app.speed.limiting_Err;
    PID.steer.min_output = app.speed.limiting_Err;
    clamp_steer_output(&PID.steer);

    /* 2. 同步角度环，保持二次项关闭 */
    PID.angle.Kp = app.angle.kp_Angle;
    PID.angle.Kd = app.angle.kd_Angle;
    PID.angle.Kp2 = 0.0f;
    PID.angle.max_output = app.angle.limiting_Angle;
    PID.angle.min_output = app.angle.limiting_Angle;
    clamp_steer_output(&PID.angle);
}

/**
 * @brief 保存当前参数
 */
void config_save(void)
{
    control_apply_config();
    eeprom_flash();
}

/**
 * @brief 加载参数
 */
void config_load(void)
{
    eeprom_init();
    control_apply_config();
}

/**
 * @brief 限制转向输出幅值
 */
static void clamp_steer_output(PID_Steer *pid)
{
    if (pid->output > pid->max_output)
        pid->output = pid->max_output;
    else if (pid->output < -pid->min_output)
        pid->output = -pid->min_output;
}

static float clamp_config_percent(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 100.0f)
        return 100.0f;
    return value;
}
