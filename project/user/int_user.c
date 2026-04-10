#include "zf_common_headfile.h"
#include "int_user.h"
#include "../service/key.h"
#include "../service/menu.h"
#include "../service/speed_loop_autotune_adapter.h"

/* ��ʱ���ж����ڶ��壨��λ��ms�� */
#define TIME_0 5  /* ���Ŀ��ƻ����� */
#define TIME_1 10 /* �������������� */

/* �ڲ�˽�г�ʼ���������� */
static void hardware_init(void);
static void control_init(void);
static void app_init(void);
static void clamp_steer_output(PID_Steer *pid);
static void timer1_service_10ms(void);

/**
 * @brief ϵͳ��ʼ���ܺ���
 */
void int_user(void)
{
    hardware_init(); /* 1. Ӳ�������ʼ�� */
    control_init();  /* 2. �����㷨������ʼ�� */
    app_init();      /* 3. Ӧ���߼���ʼ�� */
}

/**
 * @brief Ӳ��������ײ������ʼ��
 */
static void hardware_init(void)
{
    /* ����ϵͳ��� */
    system_delay_init();
    ips114_init();   /* IPS ��Ļ */
    imu660rc_init(IMU660RC_QUARTERNION_120HZ); /* 6����Դ����� */
    eeprom_init();   /* ���ô洢���� */

    /* ��ʱ�� PIT ��ʼ�� */
    pit_ms_init(TIM0_PIT, TIME_0);
    pit_ms_init(TIM1_PIT, TIME_1);
    tim1_irq_handler = timer1_service_10ms;

    /* ���������������ʼ�� */
    encoder_dir_init(TIM3_ENCOEDER, IO_P46, TIM3_ENCOEDER_P04);
    encoder_dir_init(TIM4_ENCOEDER, IO_P42, TIM4_ENCOEDER_P06);

    /* ADC ͨ����ʼ�� */
    adc_init(ADC_CH13_P05, ADC_8BIT); /* ��ص�ѹ���� */
    adc_init(ADC_CH0_P10, ADC_12BIT); /* ��� 1 */
    adc_init(ADC_CH1_P11, ADC_12BIT); /* ��� 2 */
    adc_init(ADC_CH8_P00, ADC_12BIT); /* ��� 3 */
    adc_init(ADC_CH9_P01, ADC_12BIT); /* ��� 4 */

    /* Ӧ�ò����� */
    motor_Init();         /* ������� PWM ������ */
    fuya_Init();          /* ��ѹ���� PWM */
    wireless_uart_init(); /* ���ߴ��ڣ����ڵ���/���أ� */
}

static void timer1_service_10ms(void)
{
    if (!Menu_Is_Service_Enabled())
        return;

    Keystroke_Scan_10ms();
    Menu_Tick_10ms();
}

/**
 * @brief ���Ʋ����� PID ʵ����ʼ��
 */
static void control_init(void)
{
    /* �ٶȻ���ʼ����Ĭ�ϸ���һ�鰲ȫ�Ļ������� */
    pid_speed_init(&PID.left_speed, 105.0f, 20.0f, 0.0f, 10000.0f, 10000.0f);
    pid_speed_init(&PID.right_speed, 105.0f, 20.0f, 0.0f, 10000.0f, 10000.0f);

    /* ת����ǶȻ������㣬����� apply_config �� EEPROM ���� */
    pid_steer_init(&PID.steer, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    pid_steer_init(&PID.angle, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* ͬ�� EEPROM ���� */
    control_apply_config();
    speed_loop_autotune_project_init();
}

/**
 * @brief Ӧ�ó��������߼�
 */
static void app_init(void)
{
    /* ����֤Ӳ����Ԫ�����ʱ��������������ƫ�� Mahony ��̬�� */
}

/**
 * @brief ����ͬ������
 * @details ��ȫ�����ýṹ�� app �е�ֵд�뵽 PID ����ʵ����
 */
void control_apply_config(void)
{
    /* 1. ͬ��ת�򻷣����ڵ�У����� */
    PID.steer.Kp = app.speed.kp_Err;
    PID.steer.Kd = app.speed.kd_Err;
    PID.steer.Kp2 = app.speed.kp2_Err;
    PID.steer.max_output = app.speed.limiting_Err;
    PID.steer.min_output = app.speed.limiting_Err;
    clamp_steer_output(&PID.steer);

    /* 2. ͬ���ǶȻ������������ǣ����� */
    PID.angle.Kp = app.angle.kp_Angle;
    PID.angle.Kd = app.angle.kd_Angle;
    PID.angle.Kp2 = 0.0f;
    PID.angle.max_output = app.angle.limiting_Angle;
    PID.angle.min_output = app.angle.limiting_Angle;
    clamp_steer_output(&PID.angle);
}

/**
 * @brief ��������
 */
void config_save(void)
{
    control_apply_config();
    eeprom_flash();
}

/**
 * @brief ��������
 */
void config_load(void)
{
    eeprom_init();
    control_apply_config();
}

/**
 * @brief �����޷�����
 */
static void clamp_steer_output(PID_Steer *pid)
{
    if (pid->output > pid->max_output)
        pid->output = pid->max_output;
    else if (pid->output < -pid->min_output)
        pid->output = -pid->min_output;
}
