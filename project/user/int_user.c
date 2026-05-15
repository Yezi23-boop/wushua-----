#include "zf_common_headfile.h"
#include "int_user.h"
#include "../service/key.h"
#include "../service/menu.h"

/* 定时器中断周期定义（单位：ms） */
#define TIME_0 5  /* 主控控制环周期 */
#define TIME_1 10 /* 按键与菜单服务周期 */

/* 内部私有初始化函数声明 */
static void hardware_init(void);
static void control_init(void);
static void app_init(void);
static void clamp_steer_output(PID_Steer *pid);
static float clamp_gyro_feedback_scale(float value);
static float clamp_config_percent(float value);
static int8 control_is_executable_element(int16 element);
static void control_set_default_element_sequence(void);
static void control_validate_element_sequence(void);
static void timer1_service_10ms(void);

/**
 * @brief 系统初始化函数
 */
void int_user(void)
{
    hardware_init(); /* 1. 硬件平台初始化 */
    control_init();  /* 2. 控制算法参数初始化 */
    app_init();      /* 3. 应用逻辑初始化 */
                     /* 定时器 PIT 初始化 */
    pit_ms_init(TIM0_PIT, TIME_0);
    pit_ms_init(TIM1_PIT, TIME_1);
}

/**
 * @brief 硬件平台与底层驱动初始化
 * @details 包含时钟频率设定（要求 40MHz）、各类外设及传感器的启动配置。
 * 初始化过程严格遵循芯片寄存器开销及时序依赖。
 * 无 RTOS，所有外设通过裸机驱动接入；初始化过程顺序需稳定，避免总线访问冲突。
 */
static void hardware_init(void)
{
    /* 初始化系统时钟 */
    system_delay_init();
    ips114_init();                             /* IPS 屏幕 */
    imu660rc_init(IMU660RC_QUARTERNION_240HZ); /* 六轴惯导初始化 */
    eeprom_init();                             /* 参数存储模块 */

    tim1_irq_handler = timer1_service_10ms;

    /* P36/P43 都按准双向口直接写端口锁存，减少 GPIO 初始化对现场接线状态的影响。 */
    P36 = 1;
    P43 = 1;

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
    fuya_init();          /* 负压风扇 PWM */
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
 * @details 基于出厂安全参数完成所有算法环、控制结构（速度、转向差速环）的默认启动值。
 *          同时需进行 EEPROM 加载同步以保障非易失性数据下发。
 */
static void control_init(void)
{
    /* 速度环初始化，默认提供一组安全基础参数 */
    pid_speed_init(&PID.left_speed, 120.0f, 25.0f, 0.0f, 9000.0f, 9000.0f);
    pid_speed_init(&PID.right_speed, 120.0f, 25.0f, 0.0f, 9000.0f, 9000.0f);

    /* 转向差速控制器先清零，具体参数由 apply_config 从 EEPROM 同步 */
    pid_steer_init(&PID.steer, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    /* 角速度内环控制器独立实例，避免与外环共享状态 */
    pid_steer_init(&PID.angle, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* 同步 EEPROM 参数 */
    control_apply_config();
}

/**
 * @brief 应用层启动逻辑
 * @details 进行高层逻辑相关的依赖初始化或预处理，如上电初始静态环境标定（IMU 陀螺仪 Z 轴零偏采集）。
 */
static void app_init(void)
{
    /* 上电时在静止状态下采集 gyro_z 零偏，后续控制环自动去零飘。 */
    imu_calibrate_gyro_z_zero_drift();
}

/**
 * @brief 判断元素编号当前是否可执行。
 * @param element 元素编号：0空、1左环、2右环预留、3圆桶、4墙面、5跷跷板。
 * @return int8 1-当前可执行，0-需要跳过。
 */
static int8 control_is_executable_element(int16 element)
{
    if (element == TRACK_ELEMENT_LEFT_RING ||
        element == TRACK_ELEMENT_CYLINDER ||
        element == TRACK_ELEMENT_WALL)
    {
        return 1;
    }
    if (element == TRACK_ELEMENT_SEESAW && app.fly.fly_ramp_enable == 1)
    {
        return 1;
    }
    return 0;
}

/**
 * @brief 将运行参数中的赛道元素序列恢复为默认值。
 *
 * 只修改 RAM 中的 app 配置；是否写入 EEPROM 由菜单保存流程决定。
 */
static void control_set_default_element_sequence(void)
{
    app.start.element_len = TRACK_ELEMENT_DEFAULT_LEN;
    app.start.element_seq[0] = TRACK_ELEMENT_DEFAULT_0;
    app.start.element_seq[1] = TRACK_ELEMENT_DEFAULT_1;
    app.start.element_seq[2] = TRACK_ELEMENT_DEFAULT_2;
    app.start.element_seq[3] = TRACK_ELEMENT_DEFAULT_3;
    app.start.element_seq[4] = TRACK_ELEMENT_DEFAULT_4;
    app.start.element_seq[5] = TRACK_ELEMENT_DEFAULT_5;
}

/**
 * @brief 校验 EEPROM 读入的元素序列并在 RAM 中兜底。
 *
 * 旧 EEPROM 新槽位可能含随机值；该函数保证 5ms 仲裁只会读到有界长度和合法元素编号。
 */
static void control_validate_element_sequence(void)
{
    uint8 i;
    uint8 has_executable;

    if (app.start.element_len < 1 || app.start.element_len > TRACK_ELEMENT_SEQUENCE_MAX)
    {
        control_set_default_element_sequence();
        return;
    }

    has_executable = 0;
    for (i = 0; i < TRACK_ELEMENT_SEQUENCE_MAX; i++)
    {
        if (app.start.element_seq[i] < TRACK_ELEMENT_NONE ||
            app.start.element_seq[i] > TRACK_ELEMENT_SEESAW)
        {
            app.start.element_seq[i] = TRACK_ELEMENT_NONE;
        }
        if (i < (uint8)app.start.element_len &&
            control_is_executable_element(app.start.element_seq[i]) != 0)
        {
            has_executable = 1;
        }
    }

    if (has_executable == 0)
    {
        control_set_default_element_sequence();
    }
}

/**
 * @brief 控制参数同步函数
 * @details 把全局配置结构 app 中的值写入 PID 运行实例
 */
void control_apply_config(void)
{
    float angle_limit;

    app.start.fuya_xili = clamp_config_percent(app.start.fuya_xili);
    app.start.fuya_wall_percent = clamp_config_percent(app.start.fuya_wall_percent);
    if (app.start.track_mode < 0 || app.start.track_mode > 3)
    {
        app.start.track_mode = 0;
    }
    control_validate_element_sequence();

    /* 1. 同步转向环，包含二次校正项 */
    PID.steer.Kp = app.speed.kp_Err;
    PID.steer.Kd = app.speed.kd_Err;
    PID.steer.Kp2 = app.speed.kp2_Err;
    PID.steer.gyro_damp = app.speed.gyro_damp_Err;
    PID.steer.max_output = app.speed.limiting_Err;
    PID.steer.min_output = app.speed.limiting_Err;
    clamp_steer_output(&PID.steer);

    /* 2. 同步角速度内环，直接使用独立限幅参数 */
    angle_limit = app.angle.limiting_Angle;
    app.angle.gyro_feedback_scale = clamp_gyro_feedback_scale(app.angle.gyro_feedback_scale);

    PID.angle.Kp = app.angle.kp_Angle;
    PID.angle.Kd = app.angle.kd_Angle;
    PID.angle.Kp2 = 0.0f;
    PID.angle.gyro_damp = 0.0f;
    PID.angle.max_output = angle_limit;
    PID.angle.min_output = angle_limit;
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

static float clamp_gyro_feedback_scale(float value)
{
    if (value < 1.0f)
        return 1.0f;
    if (value > 50.0f)
        return 50.0f;
    return value;
}
