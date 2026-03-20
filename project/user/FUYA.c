#include "zf_common_headfile.h"
#include "FUYA.h"
#include <math.h>

/* --- 全局状态变量 --- */
volatile int fuya_date = 0;             /* 当前输出给负压执行器的平滑占空比 */
volatile float fuya_date_factor = 0.0f; /* 目标占空比，用于调试观察 */
volatile uint8 phase = 0;               /* 当前所处运动阶段（0~4） */

/* --- 负压基准参数 (占空比量程 0~10000) --- */
float DUTY_BOTTOM = 0;           /* 平地段吸力基准（由配置决定） */
#define DUTY_LEFT 4500.0f        /* 左侧竖墙吸力基准 */
#define DUTY_RIGHT 4000.0f       /* 右侧竖墙吸力基准 */
#define DUTY_TOP 4000.0f         /* 天花板（倒立）吸力基准 */
#define PREFERENCE_OFFSET 500.0f /* 前向优待最大修正偏移 */

/* --- 姿态判定阈值 (基于单位向量分量) --- */
#define VZC_GROUND_THRESH 0.93f   /* 判定为平地的 Z 分量阈值 */
#define VZC_VERTICAL_THRESH 0.15f /* 判定为竖直墙面的 Z 分量阈值 */
#define VZC_INVERT_THRESH -0.9f   /* 判定为完全倒立的 Z 分量阈值 */
#define VXC_PLANE_MARGIN 0.06f    /* X 分量平地余量 */
#define VXC_WALL_MARGIN 0.06f     /* X 分量墙面余量 */
#define VXC_WALL_RELEASE 0.02f    /* X 分量释放阈值 */

/* --- 动态滤波系数 (Alpha) --- */
#define ALPHA_UP1 0.92f    /* 阶段 0/1 的响应速度 */
#define ALPHA_UP2 0.65f    /* 阶段 2 的响应速度 */
#define ALPHA_DOWN1 0.55f  /* 阶段 3 的响应速度 */
#define ALPHA_DOWN2 0.45f  /* 阶段 4 的响应速度 */
#define ALPHA_DEFAULT 0.8f /* 默认响应速度 */

/**
 * @brief 负压风扇硬件初始化
 */
void fuya_Init(void)
{
    /* 初始化 PWMA 通道 2N，引脚 P03，频率 17kHz，用于控制负压风扇 */
    pwm_init(PWMA_CH2N_P03, 17000, 0);
}

/**
 * @brief 输出 PWM 占空比到负压执行器
 * @param pwm 占空比值 (0~4000)
 */
void fuya_motor_output(int pwm)
{
    /* 安全限幅 */
    if (pwm < 0)
        pwm = 0;
    if (pwm > 4000)
        pwm = 4000;
    pwm_set_duty(PWMA_CH2N_P03, (uint32)pwm);
}

/**
 * @brief 根据姿态向量判定当前的运动阶段
 * @details
 * 0: 平地
 * 1: 平地 -> 左墙 (上墙)
 * 2: 左墙 -> 天花板
 * 3: 天花板 -> 右墙 (下墙)
 * 4: 右墙 -> 平地
 */
uint8 get_current_phase(float vzc, float vxc)
{
    if (vzc >= VZC_GROUND_THRESH && (float)fabs(vxc) <= VXC_PLANE_MARGIN)
    {
        return 0;
    }
    else if (vzc >= VZC_VERTICAL_THRESH && vzc < VZC_GROUND_THRESH && vxc <= -VXC_WALL_MARGIN)
    {
        return 1;
    }
    else if (vzc >= -1.0f && vzc <= VZC_VERTICAL_THRESH && vxc <= VXC_WALL_RELEASE)
    {
        return 2;
    }
    else if (vzc >= -1.0f && vzc <= VZC_VERTICAL_THRESH && vxc > VXC_WALL_RELEASE)
    {
        return 3;
    }
    else if (vzc >= VZC_VERTICAL_THRESH && vzc < VZC_GROUND_THRESH && vxc > VXC_WALL_MARGIN)
    {
        return 4;
    }
    return 0;
}

/**
 * @brief 计算前向优待修正值
 * @details 在墙面或倾斜场景下，根据小车航向与重力方向的夹角，微调吸力以获得更好的抓地力
 */
static float calculate_forward_preference(float base_duty, float vzc, float vxc, float vyc)
{
    float strength = 0.0f;
    float gt_norm, align_fwd, offset;

    /* 1. 计算场景强度系数 (0~1) */
    if ((float)fabs(vzc) < VZC_VERTICAL_THRESH)
    {
        strength = 1.0f;
    }
    else if (vzc > VZC_VERTICAL_THRESH && vzc < VZC_GROUND_THRESH)
    {
        strength = 1.0f - (vzc - VZC_VERTICAL_THRESH) / (VZC_GROUND_THRESH - VZC_VERTICAL_THRESH);
    }
    else if (vzc > VZC_INVERT_THRESH && vzc < -VZC_VERTICAL_THRESH)
    {
        strength = 1.0f - (-VZC_VERTICAL_THRESH - vzc) / (-VZC_VERTICAL_THRESH - VZC_INVERT_THRESH);
    }

    if (strength < 0.1f)
        return base_duty;

    /* 2. 计算平面内对齐度 */
    gt_norm = (float)sqrt(vxc * vxc + vyc * vyc);
    align_fwd = (gt_norm > 1e-6f) ? (float)fabs(vxc) / gt_norm : 0.5f;
    align_fwd = func_limit_ab(align_fwd, 0.0f, 1.0f);

    /* 3. 叠加偏移量 */
    offset = (align_fwd - 0.5f) * PREFERENCE_OFFSET;
    return base_duty + strength * offset;
}

/**
 * @brief 计算各阶段的基础目标负压
 */
static float calculate_base_duty(uint8 p, float vzc)
{
    float ratio, delta;
    DUTY_BOTTOM = app.start.fuya_xili;

    switch (p)
    {
    case 0:
        return DUTY_BOTTOM;
    case 1: /* 指数型过渡：平地 -> 左墙 */
        ratio = 1.0f - vzc;
        delta = 1.0f - (float)exp(-3.0f * ratio);
        return DUTY_BOTTOM + (DUTY_LEFT - DUTY_BOTTOM) * func_limit_ab(delta, 0.0f, 1.0f);
    case 2: /* 墙面 -> 天花板 */
        ratio = -vzc;
        delta = 1.0f - (float)exp(-3.0f * ratio);
        return DUTY_LEFT - (DUTY_LEFT - DUTY_TOP) * func_limit_ab(delta, 0.0f, 1.0f);
    case 3: /* 天花板 -> 右墙 */
        ratio = vzc + 1.0f;
        delta = 1.0f - (float)exp(-3.0f * ratio);
        return DUTY_TOP + (DUTY_RIGHT - DUTY_TOP) * func_limit_ab(delta, 0.0f, 1.0f);
    case 4: /* 右墙 -> 平地 */
        ratio = vzc;
        delta = 1.0f - (float)exp(-3.0f * ratio);
        return DUTY_RIGHT - (DUTY_RIGHT - DUTY_BOTTOM) * func_limit_ab(delta, 0.0f, 1.0f);
    default:
        return 1800.0f;
    }
}

/**
 * @brief 负压控制核心更新函数
 */
void fuya_update_simple(void)
{
    float vzc, vxc, vyc, base_duty, target_duty, alpha;

    /* 1. 获取并限制传感器输入 */
    vzc = func_limit(vz, 1.0f);
    vxc = func_limit(vx, 1.0f);
    vyc = func_limit(vy, 1.0f);

    /* 2. 状态判定与基础计算 */
    phase = get_current_phase(vzc, vxc);
    base_duty = calculate_base_duty(phase, vzc);

    /* 3. 叠加高级修正 */
    target_duty = calculate_forward_preference(base_duty, vzc, vxc, vyc);

    /* 4. 执行动态滤波平滑 */
    switch (phase)
    {
    case 0:
        alpha = ALPHA_UP1;
        break;
    case 1:
        alpha = ALPHA_UP2;
        break;
    case 2:
        alpha = ALPHA_DOWN1;
        break;
    case 3:
        alpha = ALPHA_DOWN2;
        break;
    default:
        alpha = ALPHA_DEFAULT;
        break;
    }
    fuya_date = (int)((float)fuya_date + alpha * (target_duty - (float)fuya_date));
    fuya_date_factor = target_duty;

    /* 5. 最终物理输出 */
    /* 注意：此处当前硬编码为使用配置值，若需动态调节可改为 fuya_date */
    fuya_motor_output((int)app.start.fuya_xili);
}
