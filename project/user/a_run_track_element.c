/**
 * @file a_run_track_element.c
 * @brief 环岛、圆桶、跷跷板与墙面赛道元素仲裁状态机。
 * @details
 * 本文件只负责按 app.start.element_len 和 app.start.element_seq[] 串行开放元素识别，
 * 具体左圆环、圆桶和墙面状态机分别放在对应模块中，避免单文件过长。
 */
#include "zf_common_headfile.h"
#include "a_run_ring.h"
#include "a_run_cylinder.h"
#include "a_run_wall.h"

enum TrackElement
{
    ELEMENT_NONE = TRACK_ELEMENT_NONE,             /**< 无特殊元素；保留给后续模式切换或保护降级。 */
    ELEMENT_LEFT_RING = TRACK_ELEMENT_LEFT_RING,   /**< 左圆环流程，接入序列表串行仲裁。 */
    ELEMENT_RIGHT_RING = TRACK_ELEMENT_RIGHT_RING, /**< 右圆环流程，复用圆环状态机并反向控制。 */
    ELEMENT_CYLINDER = TRACK_ELEMENT_CYLINDER,     /**< 圆桶流程，保持菜单显示值 3 不变。 */
    ELEMENT_WALL = TRACK_ELEMENT_WALL,             /**< 墙面流程，保持菜单显示值 4 不变。 */
    ELEMENT_SEESAW = TRACK_ELEMENT_SEESAW          /**< 跷跷板流程，复用 a_run_fly 的弱磁/恢复状态机。 */
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
        element == ELEMENT_SEESAW)
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
    a_run_ring_reset();
    a_run_cylinder_reset();
    a_run_wall_reset();
    /*
     * 跷跷板完成事件只推进元素序列，COOLDOWN 还要继续释放速度。
     * 切到任意后续元素时都不能清掉 fly_release_speed，否则会一拍回到巡线速度。
     */
    if (element == ELEMENT_NONE || flat_fly != FLY_STATE_COOLDOWN)
    {
        a_run_fly_reset();
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
 * @param start_index 起始扫描下标，超过有效长度时会从 0 折回。
 *
 * 扫描范围受 element_len 限制，长度非法或全空时进入无元素状态。
 */
static void track_element_enter_from_index(uint8 start_index)
{
    uint8 scan_count;
    uint8 index;
    uint8 element_len;
    int element;

    if (app.start.element_len < 1 || app.start.element_len > TRACK_ELEMENT_SEQUENCE_MAX)
    {
        track_element_enter(ELEMENT_NONE);
        return;
    }

    element_len = (uint8)app.start.element_len;
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
 * 元素顺序由 `app.start.element_len` 和 `app.start.element_seq[]` 决定，0/不可执行槽位会跳过。
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

    switch (expected_element)
    {
    case ELEMENT_LEFT_RING:
        if (a_run_ring_update_5ms(1) != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_RIGHT_RING:
        if (a_run_ring_update_5ms(-1) != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_CYLINDER:
        if (a_run_cylinder_update_5ms() != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_SEESAW:
        a_run_fly_update_speed(speed, 1);
        if (a_run_fly_take_finish_event() != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    case ELEMENT_WALL:
        if (a_run_wall_update_5ms() != 0)
        {
            track_element_enter_from_index((uint8)(element_index + 1));
        }
        break;

    default:
        // 当前处于 ELEMENT_NONE 或未知状态，不执行任何元素逻辑
        break;
    }

    a_run_fly_update_release_speed(speed);
    a_run_ring_update_angle_target(angle_target);
}
