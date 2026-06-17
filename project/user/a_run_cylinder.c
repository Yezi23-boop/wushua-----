/**
 * @file a_run_cylinder.c
 * @brief 圆桶强信号窗口与编码器积分状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_cylinder.h"

#define CYLINDER_AD_SINGLE_HIGH_THRESHOLD 80   /* 圆桶单路强信号阈值：ad1 或 ad4 任一路超过该值也算一次命中。 */
#define CYLINDER_AD_VERTICAL_HIGH_THRESHOLD 80 /* 圆桶纵向强信号阈值：ad2 或 ad3 任一路超过该值也算一次命中。 */
#define CYLINDER_TOP_WINDOW_COUNT 250u         /* 圆桶命中统计窗口，2ms * 250 = 500ms。 */
#define CYLINDER_TOP_HIT_COUNT 8               /* 500ms 窗口内强信号达到该次数才认定进入圆桶段。 */

enum CylinderStep
{
    CYL_IDLE = A_RUN_CYLINDER_STATE_IDLE,
    CYL_WAIT_TOP = A_RUN_CYLINDER_STATE_WAIT_TOP,
    CYL_WAIT_GROUND = A_RUN_CYLINDER_STATE_WAIT_GROUND
};

static enum CylinderStep cylinder_state = CYL_IDLE; /**< 圆桶状态机阶段，由 2ms 主环推进。 */
static uint8 cylinder_top_count = 0;                /**< 圆桶窗口内命中次数，达到阈值后认定进入圆桶段。 */
static uint8 cylinder_top_window_count = 0;         /**< 圆桶命中统计窗口计数，首个强信号后开始计时。 */
static float cylinder_encoder_sum = 0.0f;           /**< 圆桶编码器里程积分，用于判断行驶距离。 */

/**
 * @brief 读取当前圆桶状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-编码器积分等待。
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
    cylinder_encoder_sum = 0.0f;
    cylinder_state = CYL_IDLE;
}

/**
 * @brief 更新圆桶过顶与编码器积分状态机。
 * @return uint8 1-圆桶流程完成，可切入下一元素；0-仍在圆桶流程中。
 */
uint8 a_run_cylinder_update_5ms(void)
{
    uint8 cylinder_ad_high;
    int both_high_threshold;
    float encoder_target;

    cylinder_ad_high = 0;
    both_high_threshold = app.cylinder.ad_both_high_threshold;
    encoder_target = app.cylinder.encoder_target;
    if (both_high_threshold < 0)
    {
        both_high_threshold = 0;
    }
    if (encoder_target < 0.0f)
    {
        encoder_target = 0.0f;
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
        cylinder_encoder_sum = 0.0f;
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
                cylinder_encoder_sum = 0.0f;
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
        cylinder_encoder_sum += (speed_l + speed_r) * 0.5f * 0.012f;
        if (cylinder_encoder_sum >= encoder_target)
        {
            cylinder_encoder_sum = 0.0f;
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
