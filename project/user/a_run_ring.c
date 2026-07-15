/**
 * @file a_run_ring.c
 * @brief 左右圆环入口识别、共用状态机与环内控制量维护。
 */
#include "zf_common_headfile.h"
#include "a_run_ring.h"

#define RING_ENTRY_CONFIRM_COUNT 5u    /* 300ms 窗口累计命中次数，2ms 调用下最快约 10ms。 */
#define RING_YAW_DT_SCALE 0.40f        /* gyro_z 已缩放 0.005，二者相乘等效 2ms 角度积分。 */
#define RING_DRIVE_OUT_AD_THRESHOLD 5u /* 出环结束时外侧电感和 ad5 的共同阈值。 */

static int8 ring_is_left_entry_signal(void);
static int8 ring_is_right_entry_signal(void);

static RingState ring_state = RING_STATE_IDLE; /**< 当前圆环状态机阶段，左右圆环共用。 */
RingStruct ring_data = {0};                    /**< 环岛过程数据，菜单和调试界面允许直接读取。 */
static uint8 ring_entry_count = 0;             /**< 300ms 窗口内的圆环入口累计命中次数。 */

/**
 * @brief 根据环岛状态更新角速度目标。
 * @param angle_target 指向目标角速度的指针，由调用方提供上下文。
 */
void a_run_ring_update_angle_target(float *angle_target)
{
    if (ring_data.diff_set != 0.0f)
    {
        *angle_target = ring_data.diff_set;
    }
}

/**
 * @brief 读取当前环岛状态机阶段。
 * @return RingState 当前圆环状态。
 */
RingState a_run_ring_get_state(void)
{
    return ring_state;
}

/**
 * @brief 复位环岛状态机和环岛输出覆盖量。
 *
 * 菜单关闭圆环或元素切换时清掉阶段、计时和目标角速度覆盖，避免残留控制量影响主控链路。
 */
void a_run_ring_reset(void)
{
    timedestroy(&ring_data.time_l);
    timedestroy(&ring_data.time_r);
    timedestroy(&ring_data.ing_ring_time);
    timedestroy(&ring_data.out_ring_time);

    ring_data.flast_l = 0;
    ring_data.flast_r = 0;
    ring_data.last_yaw = 0;
    ring_data.diff_set = 0;
    ring_data.distance = 0;
    ring_data.encoder = 0;
    ring_data.gyro_flat = 0;
    ring_data.yaw_delta_sum = 0;
    ring_entry_count = 0;
    ring_state = RING_STATE_IDLE;
}

/**
 * @brief 判断左环入口电感特征是否命中。
 * @return int8 1-命中左环入口特征，0-未命中。
 */
static int8 ring_is_left_entry_signal(void)
{
    if (ad1 > 30 &&
        ad2 > 5 &&
        ad3 > 5 &&
        ad4 > 30 &&
        ad5 > 20)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断右环入口电感特征是否命中。
 * @return int8 1-命中右环入口特征，0-未命中。
 */
static int8 ring_is_right_entry_signal(void)
{
    /* 右环入口复用左环对称条件，方向由 ring_dir=-1 控制 */
    if (ad1 > 30 &&
        ad2 > 5 &&
        ad3 > 5 &&
        ad4 > 30 &&
        ad5 > 20)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 按 2ms 主控制周期更新圆环状态机。
 * @param ring_dir 圆环方向：1-左圆环，-1-右圆环。
 * @return uint8 1-当前圆环流程完成，0-未完成。
 */
uint8 a_run_ring_update_2ms(int8 ring_dir)
{
    int8 entry_signal;

    if (ring_dir >= 0)
    {
        ring_dir = 1;
    }
    else
    {
        ring_dir = -1;
    }

    a_run_ring_update_integrals();
    switch (ring_state)
    {
    case RING_STATE_IDLE:
        if (ring_dir > 0)
        {
            entry_signal = ring_is_left_entry_signal();
        }
        else
        {
            entry_signal = ring_is_right_entry_signal();
        }
        if (entry_signal != 0)
        {
            ring_entry_count++;
        }

        if (ring_entry_count > 0)
        {
            /* 首次命中后开启窗口，窗口内累计命中次数，避免单拍电感抖动打断进环确认。 */
            if (ring_entry_count >= RING_ENTRY_CONFIRM_COUNT)
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
                if (ring_dir > 0)
                {
                    ring_data.flast_l = 1;
                    ring_data.flast_r = 0;
                }
                else
                {
                    ring_data.flast_l = 0;
                    ring_data.flast_r = 1;
                }
                ring_state = RING_STATE_ENTRY;
            }
            else if (timeadd(&ring_data.time_l, 300))
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
            }
        }
        break;

    case RING_STATE_ENTRY:
        ring_data.diff_set = 0;
        ring_data.distance = 1;
        ring_data.yaw_delta_sum = 0;
        if (ring_data.encoder >= app.ring.ring_entry_encoder)
        {
            ring_data.encoder = 0;
            ring_data.distance = 1;
            ring_data.last_yaw = 0;
            ring_data.gyro_flat = 1;
            ring_data.yaw_delta_sum = 0;
            ring_state = RING_STATE_PRE_RING;
        }
        break;

    case RING_STATE_PRE_RING:
        ring_data.diff_set = app.ring.pre_ring_Gyro_target * ring_dir;
        if (ring_data.yaw_delta_sum >= app.ring.pre_ring_Gyroz)
        {
            ring_data.diff_set = 0;
            ring_state = RING_STATE_IN_RING;
        }
        break;

    case RING_STATE_IN_RING:
        if (ring_data.yaw_delta_sum >= app.ring.in_ring_Gyroz &&
            ring_data.encoder >= app.ring.in_ring_encoder)
        {
            ring_data.distance = 0;
            ring_state = RING_STATE_PRE_OUT_RING;
        }
        break;

    case RING_STATE_PRE_OUT_RING:
        ring_data.diff_set = app.ring.pre_out_ring_Gyro_target * ring_dir;
        if (ring_data.yaw_delta_sum >= app.ring.pre_out_ring_Gyroz)
        {
					            stop = 1;
            ring_data.diff_set = -5 * ring_dir;
            ring_data.encoder = 0;
            ring_data.distance = 1;
            ring_state = RING_STATE_DRIVE_OUT_RING;
        }
        break;

    case RING_STATE_DRIVE_OUT_RING:
        ring_data.diff_set = -5 * ring_dir;
        if (ring_data.encoder >= app.ring.drive_out_ring_encoder &&
            ad5 < RING_DRIVE_OUT_AD_THRESHOLD &&
            ((ring_dir < 0 && ad1 < RING_DRIVE_OUT_AD_THRESHOLD) ||
             (ring_dir > 0 && ad4 < RING_DRIVE_OUT_AD_THRESHOLD)))
        {
            ring_data.diff_set = 0;
            ring_data.distance = 0;
            ring_data.gyro_flat = 0;
            ring_data.yaw_delta_sum = 0;
            timedestroy(&ring_data.out_ring_time);
            ring_state = RING_STATE_OUT_RING;
        }
        break;

    case RING_STATE_OUT_RING:
        if (timeadd(&ring_data.out_ring_time, 200))
        {
            timedestroy(&ring_data.out_ring_time);
            ring_data.flast_l = 0;
            ring_data.flast_r = 0;
            ring_data.last_yaw = 0;
            ring_data.diff_set = 0;
            ring_data.distance = 0;
            ring_data.encoder = 0;
            ring_data.gyro_flat = 0;
            ring_data.yaw_delta_sum = 0;
            ring_state = RING_STATE_IDLE;
            return 1;
        }
        break;

    default:
        a_run_ring_reset();
        break;
    }

    return 0;
}

/**
 * @brief 更新环岛判定所需的里程与转角量。
 *
 * `encoder` 按左右轮平均速度和现场标定系数累计里程；`yaw_delta_sum` 使用 `gyro_z` 的绝对值积分。
 */
void a_run_ring_update_integrals(void)
{
    float delta_angle;

    if (ring_data.gyro_flat == 1)
    {
        delta_angle = gyro_z;
        if (delta_angle < 0.0f)
        {
            delta_angle = -delta_angle;
        }

        ring_data.yaw_delta_sum += delta_angle * RING_YAW_DT_SCALE;
    }

    if (ring_data.distance == 1)
    {
        /* 0.012f：里程积分系数，由采样周期(2ms)和轮径/编码器标定共同决定，将速度值转为每周期行驶距离(cm)。 */
        ring_data.encoder += (speed_l + speed_r) * 0.5f * 0.012f;
    }
}
