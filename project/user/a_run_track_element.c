/**
 * @file a_run_track_element.c
 * @brief 环岛、圆桶、跷跷板与墙面赛道元素仲裁状态机。
 * @details
 * 本文件只负责按 app.start.element_seq[] 串行开放元素识别，
 * 具体左圆环、圆桶和墙面状态机分别放在对应模块中，避免单文件过长。
 */
#include "zf_common_headfile.h"
#include "a_run_ring.h"
#include "a_run_cylinder.h"
#include "a_run_wall.h"
#include "a_run_cross.h"

#define ADC_SEESAW_CENTER_A_1 1.50f /* 跷跷板前挪/恢复期横向主差分权重，强调左右主电感居中。 */
#define ADC_SEESAW_CENTER_B_1 0.50f /* 跷跷板前挪/恢复期降低竖向差分影响，减少启动串道。 */
#define ADC_SEESAW_CENTER_C_L 1.00f /* 跷跷板前挪/恢复期弱信号分母补偿，抑制偏差突变。 */

enum TrackElement
{
    ELEMENT_NONE = TRACK_ELEMENT_NONE,                /**< 无特殊元素；保留给后续模式切换或保护降级。 */
    ELEMENT_LEFT_RING = TRACK_ELEMENT_LEFT_RING,      /**< 左圆环流程，接入序列表串行仲裁。 */
    ELEMENT_RIGHT_RING = TRACK_ELEMENT_RIGHT_RING,    /**< 右圆环流程，复用圆环状态机并反向控制。 */
    ELEMENT_CYLINDER = TRACK_ELEMENT_CYLINDER,        /**< 圆桶流程，保持菜单显示值 3 不变。 */
    ELEMENT_WALL = TRACK_ELEMENT_WALL,                /**< 墙面流程，保持菜单显示值 4 不变。 */
    ELEMENT_SEESAW = TRACK_ELEMENT_SEESAW,            /**< 跷跷板流程，复用 a_run_fly 的弱磁/恢复状态机。 */
    ELEMENT_CROSS = TRACK_ELEMENT_CROSS,              /**< 双十字流程，电感和命中后编码器积分退出。 */
    ELEMENT_CROSS_SINGLE = TRACK_ELEMENT_CROSS_SINGLE /**< 单十字流程，入口判定与双十字相同，仅序列区分。 */
};

static int8 track_element_is_executable(int element);
static void track_element_enter(enum TrackElement element);
static void track_element_enter_from_index(uint8 start_index);
static void track_element_reset_state(void);

static enum TrackElement expected_element = ELEMENT_NONE; /**< 当前期望赛道元素，用于串行屏蔽非当前元素的入口识别。 */
static uint8 element_index = 0;                           /**< 当前元素序列下标，只在 2ms 元素仲裁中更新。 */
static uint8 element_sequence_started = 0;                /**< 元素识别开启后是否已经按 E1~E6 完成首元素初始化。 */

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶，4-墙面，5-跷跷板。
 */
int8 a_run_track_element_get_expected_element(void)
{
    return (int8)expected_element;
}

/**
 * @brief 按当前激活元素覆盖转向环参数。
 * @param kp 转向环比例系数指针，调用方需先写入全局默认值。
 * @param kd 转向环微分系数指针，调用方需先写入全局默认值。
 * @param kp2 转向环非线性增强系数指针，调用方需先写入全局默认值。
 *
 * 优先级从高到低：单十字TIMING > 双十字TIMING > 圆桶DECEL > 圆环有效阶段。
 * 圆桶与圆环由序列仲裁保证互斥，DECEL 优先于圆环是历史语义（else 分支保留）；
 * 双/单十字状态机同时处于 TIMING 不可能发生，双分支保留原顺序兜底。
 * 无元素激活时不写指针，全局默认值直接生效。
 */
void a_run_track_element_apply_steer_params(float *kp, float *kd, float *kp2)
{
    if (a_run_cylinder_get_state() == CYLINDER_STATE_DECEL)
    {
        /* 圆桶减速阶段沿用圆桶专用Kp/Kd，Kp2保持全局值。 */
        *kp = app.cylinder.kp_Err;
        *kd = app.cylinder.kd_Err;
    }
    else
    {
        a_run_ring_apply_steer_params(kp, kd, kp2);
    }
    if (a_run_cross_get_state() == CROSS_STATE_TIMING)
    {
        *kp = app.cross.kp_Err;
        *kd = app.cross.kd_Err;
        *kp2 = app.cross.kp2_Err;
    }
    if (a_run_cross_single_get_state() == CROSS_SINGLE_STATE_TIMING)
    {
        *kp = app.cross_single.kp_Err;
        *kd = app.cross_single.kd_Err;
        *kp2 = app.cross_single.kp2_Err;
    }
}

/**
 * @brief 按当前激活元素覆盖电感差比和解算的ABC权重。
 * @param a_value 横向主差分权重指针，调用方需先写入全局默认值。
 * @param b_value 竖向差分权重指针，调用方需先写入全局默认值。
 * @param c_value 分母补偿权重指针，调用方需先写入全局默认值。
 *
 * 顺序覆盖、后写生效，优先级从高到低：
 * 圆环有效阶段 > 单十字TIMING > 双十字TIMING > 跷跷板居中 > 圆桶DECEL。
 * 圆环最后施加优先级最高，保持原语义（进出环必须严格跟随专用权重）；
 * 只改本次解算局部权重，不动 app.angle，异常退出后全局值自动恢复。
 * 无元素激活时不写指针，全局默认值直接生效。
 */
void a_run_track_element_apply_adc_params(float *a_value, float *b_value, float *c_value)
{
    if (a_run_cylinder_get_state() == CYLINDER_STATE_DECEL)
    {
        /* 圆桶窗口确认后才切专用ABC，避免序列轮到圆桶但尚未识别时削弱普通循迹。 */
        *a_value = app.cylinder.adc_a_1;
        *b_value = app.cylinder.adc_b_1;
        *c_value = app.cylinder.adc_c_l;
    }
    if (seesaw_centering_active != 0)
    {
        /* 前挪和落地恢复阶段优先贴主横向中线，降低刚起步时竖向差分把车带向旁线的风险。 */
        *a_value = ADC_SEESAW_CENTER_A_1;
        *b_value = ADC_SEESAW_CENTER_B_1;
        *c_value = ADC_SEESAW_CENTER_C_L;
    }
    if (a_run_cross_get_state() == CROSS_STATE_TIMING)
    {
        *a_value = app.cross.adc_a_1;
        *b_value = app.cross.adc_b_1;
        *c_value = app.cross.adc_c_l;
    }
    if (a_run_cross_single_get_state() == CROSS_SINGLE_STATE_TIMING)
    {
        *a_value = app.cross_single.adc_a_1;
        *b_value = app.cross_single.adc_b_1;
        *c_value = app.cross_single.adc_c_l;
    }
    a_run_ring_apply_adc_params(a_value, b_value, c_value);
}

/**
 * @brief 判断元素编号当前是否可由仲裁状态机执行。
 * @param element 元素编号，来源于 app.start.element_seq。
 * @return int8 1-可执行，0-应跳过。
 */
static int8 track_element_is_executable(int element)
{
    if (element == ELEMENT_LEFT_RING ||
        element == ELEMENT_RIGHT_RING ||
        element == ELEMENT_CYLINDER ||
        element == ELEMENT_WALL ||
        element == ELEMENT_SEESAW ||
        element == ELEMENT_CROSS ||
        element == ELEMENT_CROSS_SINGLE)
    {
        return 1;
    }
    return 0;
}

/**
 * @brief 进入指定赛道元素并清理其他元素残留状态。
 * @param element 目标元素编号，通常来自当前元素序列槽位。
 */
static void track_element_enter(enum TrackElement element)
{
    /* 圆环出环释放期间不清状态机，否则释放过程会被元素切换打断。 */
    if (a_run_ring_get_state() != RING_STATE_RELEASE)
    {
        a_run_ring_reset();
    }
    if (element == ELEMENT_NONE ||
        element == ELEMENT_CYLINDER ||
        a_run_cylinder_get_state() != CYLINDER_STATE_RELEASE)
    {
        a_run_cylinder_reset();
    }
    a_run_wall_reset();
    a_run_cross_reset();
    a_run_cross_single_reset();
    /*
     * 跷跷板完成事件只推进元素序列，RELEASE 还要继续释放速度。
     * 切到任意后续元素时都不能清掉 seesaw_release_speed，否则会一拍回到巡线速度。
     */
    if (element == ELEMENT_NONE || a_run_seesaw_get_state() != SEESAW_STATE_RELEASE)
    {
        a_run_seesaw_reset();
    }

    expected_element = element;
    if (element == ELEMENT_NONE)
    {
        element_index = 0;
        return;
    }
}

/**
 * @brief 从指定序列下标开始寻找并进入第一个可执行元素。
 * @param start_index 起始扫描下标，超过有效范围时会从 0 折回。
 *
 * 有效长度由 element_seq 中首个 NONE(0) 终止符动态确定。
 */
static void track_element_enter_from_index(uint8 start_index)
{
    uint8 scan_count;
    uint8 index;
    uint8 element_len;
    int element;

    // 动态扫描有效长度：遇到第一个 NONE 终止
    element_len = 0;
    while (element_len < TRACK_ELEMENT_SEQUENCE_MAX && app.start.element_seq[element_len] != TRACK_ELEMENT_NONE)
    {
        element_len++;
    }

    if (element_len == 0)
    {
        track_element_enter(ELEMENT_NONE);
        return;
    }

    index = start_index;
    for (scan_count = 0; scan_count < element_len; scan_count++)
    {
        if (index >= element_len)
        {
            index = 0;
        }

        element = app.start.element_seq[index];
        if (track_element_is_executable(element) != 0)
        {
            element_index = index;
            track_element_enter((enum TrackElement)element);
            return;
        }

        index++;
    }

    track_element_enter(ELEMENT_NONE);
}

/**
 * @brief 复位赛道元素仲裁状态机。
 *
 * 菜单关闭元素识别时，仲裁、环岛、圆桶、跷跷板和墙面必须同步回到初始状态。
 */
static void track_element_reset_state(void)
{
    element_sequence_started = 0;
    track_element_enter(ELEMENT_NONE);
}

/**
 * @brief 更新赛道元素仲裁状态机。
 *
 * `app.start.element_enable` 作为整体元素识别开关；开启后按 `expected_element` 开放当前元素流程。
 * 元素顺序由 `app.start.element_seq[]` 决定，遇到首个 NONE 截止；0/不可执行槽位会跳过。
 *
 * @param speed 2ms 主控制链路当前目标速度，保留小数速度设定；跷跷板和完成后释放阶段可能覆盖该值。
 * @param angle_target 转向外环输出的目标角速度，圆环和跷跷板阶段可能覆盖该值。
 */
void a_run_track_element_update_gate(float *speed, float *angle_target)
{
    if (app.start.element_enable != 1)
    {
        track_element_reset_state();
        return;
    }

    if (element_sequence_started == 0)
    {
        track_element_enter_from_index(0);
        element_sequence_started = 1;
    }

    /* 圆桶/圆环释放只提供基础速度上限，当前元素可在后续 switch 中覆盖更低速度。 */
    a_run_cylinder_update_release_speed(speed);
    a_run_ring_update_release_speed(speed);

    switch (expected_element)
    {
    case ELEMENT_LEFT_RING:
        if (a_run_ring_update_2ms(1) != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_RIGHT_RING:
        if (a_run_ring_update_2ms(-1) != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_CYLINDER:
        if (a_run_cylinder_update_5ms(speed) != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_SEESAW:
        if (app.fly.seesaw_mode == 0)
        {
            /* 飞坡模式 */
            a_run_fly_update_speed(speed, 1);
        }
        else
        {
            /* 停止等待模式 */
            a_run_seesaw_update_speed(speed, 1);
        }
        if (a_run_seesaw_take_finish_event() != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_WALL:
        if (a_run_wall_update_5ms(speed) != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_CROSS:
        if (a_run_cross_update_5ms() != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_CROSS_SINGLE:
        if (a_run_cross_single_update_5ms() != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    default:
        /* 当前处于 ELEMENT_NONE 或未知状态，不执行任何元素逻辑 */
        break;
    }

    a_run_seesaw_update_release_speed(speed);
    a_run_ring_apply_speed(speed);
    a_run_ring_update_angle_target(angle_target);
}
