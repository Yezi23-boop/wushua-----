/**
 * @file a_run_cylinder.c
 * @brief 圆桶强信号窗口、回地确认与稳定延时状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_cylinder.h"

#define CYLINDER_AD_SINGLE_HIGH_THRESHOLD 80   /* 圆桶单路强信号阈值：ad1 或 ad4 任一路超过该值也算一次命中。 */
#define CYLINDER_AD_VERTICAL_HIGH_THRESHOLD 80 /* 圆桶纵向强信号阈值：ad2 或 ad3 任一路超过该值也算一次命中。 */
#define CYLINDER_TOP_WINDOW_COUNT 250u         /* 圆桶命中统计窗口，2ms * 250 = 500ms。 */
#define CYLINDER_TOP_HIT_COUNT 8               /* 500ms 窗口内强信号达到该次数才认定进入圆桶段。 */
#define CYLINDER_GROUND_CONFIRM_COUNT 25       /* 2ms * 8 = 16ms，强信号消失后连续确认回地。 */
#define CYLINDER_STABLE_DELAY_COUNT 100u       /* 2ms * 250 = 500ms，回地稳定后切入下一元素。 */

enum CylinderStep
{
    CYL_IDLE = 0,
    CYL_WAIT_TOP = 1,
    CYL_WAIT_GROUND = 2,
    CYL_STABLE_DELAY = 3
};

static enum CylinderStep cylinder_state = CYL_IDLE; /**< 圆桶状态机阶段，由 2ms 主环推进。 */
static uint8 cylinder_top_count = 0;                /**< 圆桶窗口内命中次数，达到阈值后认定进入圆桶段。 */
static uint8 cylinder_top_window_count = 0;         /**< 圆桶命中统计窗口计数，首个强信号后开始计时。 */
static uint8 cylinder_ground_count = 0;             /**< 圆桶回地确认计数，横向强信号连续消失后才认定回地。 */
static uint8 cylinder_stable_count = 0;             /**< 圆桶回地稳定延时计数，完成后进入下一元素。 */

/**
 * @brief 读取当前圆桶状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_cylinder_get_state(void)
{
    return (int8)cylinder_state;
}

/**
 * @brief 复位圆桶状态机。
 *
 * 圆桶流程只维护本地状态计数，负压输出保持固定百分比链路。
 */
void a_run_cylinder_reset(void)
{
    cylinder_top_count = 0;
    cylinder_top_window_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_IDLE;
}

/**
 * @brief 更新圆桶过顶/回地状态机。
 * @return uint8 1-圆桶流程完成，可切入下一元素；0-仍在圆桶流程中。
 */
uint8 a_run_cylinder_update_5ms(void)
{
    uint8 cylinder_ad_high;
    int both_high_threshold;

    cylinder_ad_high = 0;
    both_high_threshold = app.cylinder.ad_both_high_threshold;
    if (both_high_threshold < 0)
    {
        both_high_threshold = 0;
    }

    if ((ad1 > (uint16)both_high_threshold &&
         ad4 > (uint16)both_high_threshold) ||
        ad1 > CYLINDER_AD_SINGLE_HIGH_THRESHOLD ||
        ad4 > CYLINDER_AD_SINGLE_HIGH_THRESHOLD ||
        ad2 > CYLINDER_AD_VERTICAL_HIGH_THRESHOLD ||
        ad3 > CYLINDER_AD_VERTICAL_HIGH_THRESHOLD)
    {
        cylinder_ad_high = 1;
    }

    switch (cylinder_state)
    {
    case CYL_IDLE:
        cylinder_top_count = 0;
        cylinder_top_window_count = 0;
        cylinder_ground_count = 0;
        cylinder_stable_count = 0;
        cylinder_state = CYL_WAIT_TOP;
        /* 刚切入圆桶时同一拍继续按 WAIT_TOP 处理，避免白白空等一个主控制环周期。 */

    case CYL_WAIT_TOP:
        if (cylinder_ad_high != 0 || cylinder_top_window_count != 0)
        {
            cylinder_top_window_count++;
            if (cylinder_ad_high != 0)
            {
                cylinder_top_count++;
            }

            if (cylinder_top_count >= CYLINDER_TOP_HIT_COUNT)
            {
                cylinder_top_count = 0;
                cylinder_top_window_count = 0;
                cylinder_ground_count = 0;
                cylinder_stable_count = 0;
                cylinder_state = CYL_WAIT_GROUND;
            }
            else if (cylinder_top_window_count >= CYLINDER_TOP_WINDOW_COUNT)
            {
                cylinder_top_count = 0;
                cylinder_top_window_count = 0;
            }
        }
        break;

    case CYL_WAIT_GROUND:
        if (cylinder_ad_high == 0)
        {
            cylinder_ground_count++;
            if (cylinder_ground_count >= CYLINDER_GROUND_CONFIRM_COUNT)
            {
                cylinder_ground_count = 0;
                cylinder_stable_count = 0;
                cylinder_state = CYL_STABLE_DELAY;
            }
        }
        else
        {
            cylinder_ground_count = 0;
        }
        break;

    case CYL_STABLE_DELAY:
        cylinder_stable_count++;
        if (cylinder_stable_count >= CYLINDER_STABLE_DELAY_COUNT)
        {
//					stop=1;
            cylinder_stable_count = 0;
            cylinder_state = CYL_IDLE;
            return 1;
        }
        break;

    default:
        a_run_cylinder_reset();
        break;
    }

    return 0;
}
