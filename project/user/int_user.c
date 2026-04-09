#include "zf_common_headfile.h"
#include "int_user.h"
#include "../service/key.h"
#include "../service/menu.h"
#include "../service/speed_loop_autotune_adapter.h"

/* 定时器中断周期定义（单位：ms） */
#define TIME_0 2  /* 核心控制环周期 */
#define TIME_1 10 /* 辅助管理环周期 */

/* 内部私有初始化函数声明 */
static void hardware_init(void);
static void control_init(void);
static void app_init(void);
static void clamp_steer_output(PID_Steer *pid);
static void timer1_service_10ms(void);

/**
 * @brief 系统初始化总函数
 */
void int_user(void)
{
    hardware_init(); /* 1. 硬件外设初始化 */
    control_init();  /* 2. 控制算法参数初始化 */
    app_init();      /* 3. 应用逻辑初始化 */
}

/**
 * @brief 硬件驱动与底层外设初始化
 */
static void hardware_init(void)
{
    /* 基础系统组件 */
    system_delay_init();
    ips114_init();   /* IPS 屏幕 */
    imu660ra_init(); /* 6轴惯性传感器 */
    eeprom_init();   /* 配置存储管理 */

    /* 定时器 PIT 初始化 */
    pit_ms_init(TIM0_PIT, TIME_0);
    pit_ms_init(TIM1_PIT, TIME_1);
    tim1_irq_handler = timer1_service_10ms;

    /* 编码器正交解码初始化 */
    encoder_dir_init(TIM3_ENCOEDER, IO_P46, TIM3_ENCOEDER_P04);
    encoder_dir_init(TIM4_ENCOEDER, IO_P42, TIM4_ENCOEDER_P06);

    /* ADC 通道初始化 */
    adc_init(ADC_CH13_P05, ADC_8BIT); /* 电池电压采样 */
    adc_init(ADC_CH0_P10, ADC_12BIT); /* 电感 1 */
    adc_init(ADC_CH1_P11, ADC_12BIT); /* 电感 2 */
    adc_init(ADC_CH8_P00, ADC_12BIT); /* 电感 3 */
    adc_init(ADC_CH9_P01, ADC_12BIT); /* 电感 4 */

    /* 应用层驱动 */
    motor_Init();         /* 电机驱动 PWM 及方向 */
    fuya_Init();          /* 负压风扇 PWM */
    wireless_uart_init(); /* 无线串口（用于调试/下载） */
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
    /* 速度环初始化：默认给定一组安全的基础参数 */
    pid_speed_init(&PID.left_speed, 105.0f, 20.0f, 0.0f, 10000.0f, 10000.0f);
    pid_speed_init(&PID.right_speed, 105.0f, 20.0f, 0.0f, 10000.0f, 10000.0f);

    /* 转向环与角度环先清零，随后由 apply_config 从 EEPROM 加载 */
    pid_steer_init(&PID.steer, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    pid_steer_init(&PID.angle, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* 同步 EEPROM 参数 */
    control_apply_config();
    speed_loop_autotune_project_init();
}

/**
 * @brief 应用程序启动逻辑
 */
static void app_init(void)
{
    /* 执行 IMU 零偏校准 */
    offset_init();
}

/**
 * @brief 参数同步函数
 * @details 将全局配置结构体 app 中的值写入到 PID 运行实例中
 */
void control_apply_config(void)
{
    /* 1. 同步转向环（基于电感）参数 */
    PID.steer.Kp = app.speed.kp_Err;
    PID.steer.Kd = app.speed.kd_Err;
    PID.steer.Kp2 = app.speed.kp2_Err;
    PID.steer.max_output = app.speed.limiting_Err;
    PID.steer.min_output = app.speed.limiting_Err;
    clamp_steer_output(&PID.steer);

    /* 2. 同步角度环（基于陀螺仪）参数 */
    PID.angle.Kp = app.angle.kp_Angle;
    PID.angle.Kd = app.angle.kd_Angle;
    PID.angle.Kp2 = 0.0f;
    PID.angle.max_output = app.angle.limiting_Angle;
    PID.angle.min_output = app.angle.limiting_Angle;
    clamp_steer_output(&PID.angle);
}

/**
 * @brief 保存配置
 */
void config_save(void)
{
    control_apply_config();
    eeprom_flash();
}

/**
 * @brief 加载配置
 */
void config_load(void)
{
    eeprom_init();
    control_apply_config();
}

/**
 * @brief 辅助限幅函数
 */
static void clamp_steer_output(PID_Steer *pid)
{
    if (pid->output > pid->max_output)
        pid->output = pid->max_output;
    else if (pid->output < -pid->min_output)
        pid->output = -pid->min_output;
}
