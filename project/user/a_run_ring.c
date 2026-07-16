/**
 * @file a_run_ring.c
 * @brief 左右圆环入口识别、运行时算法选择与环内控制量维护。
 */
#include "zf_common_headfile.h"
#include "a_run_ring.h"

#define RING_ENTRY_CONFIRM_COUNT 5u    /* 300ms窗口累计命中次数，2ms调用下最快约 10ms。 */
#define RING_YAW_DT_SCALE 0.40f        /* gyro_z已缩放0.005，二者相乘等效 2ms 角度积分。 */
#define RING_DRIVE_OUT_AD_THRESHOLD 5u /* 贝尔分支旧圆环出环时外侧电感阈值。 */

static int8 ring_is_entry_signal(void);

static RingState ring_state = RING_STATE_IDLE; /**< 当前圆环状态机阶段，左右圆环共用。 */
RingStruct ring_data = {0};                    /**< 圆环方向、积分量和软定时器。 */
static uint8 ring_entry_count = 0;             /**< 300ms窗口内累计入口命中次数。 */
static RingControlMode ring_mode_active = RING_CONTROL_SENSOR_BIAS; /**< 本圈入口确认时锁存的算法。 */
static uint8 ring_profile_active = 0;           /**< 本圈入口确认时锁存的新算法参数组。 */

#define RING_ACTIVE_PROFILE (app.ring.profiles[ring_profile_active])

/**
 * @brief 判断圆环入口电感特征是否命中。
 * @return int8 1-命中入口特征，0-未命中。
 */
static int8 ring_is_entry_signal(void)
{
    if (ad1 > 30 &&
        ad2 > 5 &&
        ad3 > 5 &&
        ad4 > 30)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 根据锁存算法和圆环阶段更新角速度目标。
 * @param angle_target 指向当前角速度目标。
 */
void a_run_ring_update_angle_target(float *angle_target)
{
    if (ring_mode_active == RING_CONTROL_SENSOR_BIAS &&
        ring_state == RING_STATE_ENTRY)
    {
        *angle_target = 0.0f;
    }
    else if (ring_data.diff_set != 0.0f)
    {
        *angle_target = ring_data.diff_set;
    }
}

/**
 * @brief 读取当前圆环状态机阶段。
 * @return RingState 当前圆环状态。
 */
RingState a_run_ring_get_state(void)
{
    return ring_state;
}

/**
 * @brief 圆环有效阶段使用独立ABC参数。
 * @param a_value 横向主差分权重指针。
 * @param b_value 辅助电感差分权重指针。
 * @param c_value 分母补偿权重指针。
 */
void a_run_ring_apply_adc_params(float *a_value, float *b_value, float *c_value)
{
    if (ring_mode_active == RING_CONTROL_SENSOR_BIAS)
    {
        if (ring_state == RING_STATE_PRE_RING ||
            ring_state == RING_STATE_IN_RING ||
            ring_state == RING_STATE_OUT_RING)
        {
            *a_value = RING_ACTIVE_PROFILE.adc_a_1;
            *b_value = RING_ACTIVE_PROFILE.adc_b_1;
            *c_value = RING_ACTIVE_PROFILE.adc_c_l;
        }
    }
    else if (ring_state != RING_STATE_IDLE && ring_state != RING_STATE_OUT_RING)
    {
        *a_value = app.ring.profiles[0].adc_a_1;
        *b_value = app.ring.profiles[0].adc_b_1;
        *c_value = app.ring.profiles[0].adc_c_l;
    }
}

/**
 * @brief 圆环有效阶段使用独立方向环参数。
 * @param kp 方向环比例系数指针。
 * @param kd 方向环微分系数指针。
 * @param kp2 方向环非线性增强系数指针。
 */
void a_run_ring_apply_steer_params(float *kp, float *kd, float *kp2)
{
    if (ring_mode_active == RING_CONTROL_SENSOR_BIAS)
    {
        if (ring_state == RING_STATE_PRE_RING ||
            ring_state == RING_STATE_IN_RING ||
            ring_state == RING_STATE_OUT_RING)
        {
            *kp = RING_ACTIVE_PROFILE.kp_Err;
            *kd = RING_ACTIVE_PROFILE.kd_Err;
            *kp2 = RING_ACTIVE_PROFILE.kp2_Err;
        }
    }
    else if (ring_state != RING_STATE_IDLE && ring_state != RING_STATE_OUT_RING)
    {
        *kp = app.ring.profiles[0].kp_Err;
        *kd = app.ring.profiles[0].kd_Err;
        *kp2 = app.ring.profiles[0].kp2_Err;
    }
}

/**
 * @brief 圆环有效阶段使用独立角速度环和差速分配参数。
 * @param kp 角速度内环比例系数指针。
 * @param kd 角速度内环微分系数指针。
 * @param inner_gain 内轮减速增益指针。
 * @param outer_gain 外轮增速增益指针。
 */
void a_run_ring_apply_angle_diff_params(float *kp,
                                        float *kd,
                                        float *inner_gain,
                                        float *outer_gain)
{
    if (ring_mode_active == RING_CONTROL_SENSOR_BIAS)
    {
        if (ring_state == RING_STATE_PRE_RING ||
            ring_state == RING_STATE_IN_RING ||
            ring_state == RING_STATE_OUT_RING)
        {
            *kp = RING_ACTIVE_PROFILE.kp_Angle;
            *kd = RING_ACTIVE_PROFILE.kd_Angle;
            *inner_gain = RING_ACTIVE_PROFILE.diff_inner_gain;
            *outer_gain = RING_ACTIVE_PROFILE.diff_outer_gain;
        }
    }
    else if (ring_state != RING_STATE_IDLE && ring_state != RING_STATE_OUT_RING)
    {
        *kp = app.ring.profiles[0].kp_Angle;
        *kd = app.ring.profiles[0].kd_Angle;
        *inner_gain = app.ring.profiles[0].diff_inner_gain;
        *outer_gain = app.ring.profiles[0].diff_outer_gain;
    }
}

/**
 * @brief 新圆环运行阶段使用锁存参数组的目标速度。
 * @param speed 当前控制链目标速度指针。
 */
void a_run_ring_apply_speed(float *speed)
{
    if (ring_mode_active == RING_CONTROL_SENSOR_BIAS &&
        (ring_state == RING_STATE_PRE_RING ||
         ring_state == RING_STATE_IN_RING ||
         ring_state == RING_STATE_OUT_RING))
    {
        *speed = RING_ACTIVE_PROFILE.target_speed;
    }
}

/**
 * @brief 新圆环进环时放大同侧电感，出环时放大对侧电感。
 * @param left_signal 左侧主电感ad1的局部浮点值。
 * @param left_middle_signal 左侧辅助电感ad2的局部浮点值。
 * @param right_middle_signal 右侧辅助电感ad3的局部浮点值。
 * @param right_signal 右侧主电感ad4的局部浮点值。
 */
void a_run_ring_apply_adc_bias(float *left_signal,
                               float *left_middle_signal,
                               float *right_middle_signal,
                               float *right_signal)
{
    if (ring_mode_active != RING_CONTROL_SENSOR_BIAS)
    {
        return;
    }

    if (ring_state == RING_STATE_PRE_RING)
    {
        if (ring_data.flast_l != 0)
        {
            *left_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;
            *left_middle_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;
        }
        else if (ring_data.flast_r != 0)
        {
            *right_middle_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;
            *right_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;
        }
    }
    else if (ring_state == RING_STATE_OUT_RING)
    {
        if (ring_data.flast_l != 0)
        {
            *right_middle_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;
            *right_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;
        }
        else if (ring_data.flast_r != 0)
        {
            *left_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;
            *left_middle_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;
        }
    }
}

/**
 * @brief 复位圆环状态机、积分量和输出覆盖量。
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
    ring_profile_active = 0;
    ring_state = RING_STATE_IDLE;
}

/**
 * @brief 按2ms主控制周期更新当前锁存算法的圆环状态机。
 * @param ring_dir 圆环方向：1-左圆环，-1-右圆环。
 * @return uint8 1-当前圆环流程完成，0-未完成。
 */
uint8 a_run_ring_update_2ms(int8 ring_dir)
{
    if (ring_dir >= 0)
    {
        ring_dir = 1;
    }
    else
    {
        ring_dir = -1;
    }

    a_run_ring_update_integrals();

    if (ring_state == RING_STATE_IDLE)
    {
        if (ring_is_entry_signal() != 0)
        {
            ring_entry_count++;
        }

        if (ring_entry_count > 0)
        {
            if (ring_entry_count >= RING_ENTRY_CONFIRM_COUNT)
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
                ring_mode_active = (RingControlMode)app.ring.control_mode;
                ring_profile_active = (uint8)app.ring.profile_select;
                ring_data.flast_l = (ring_dir > 0) ? 1 : 0;
                ring_data.flast_r = (ring_dir < 0) ? 1 : 0;
                ring_data.diff_set = 0;
                ring_data.encoder = 0;
                ring_data.yaw_delta_sum = 0;
                ring_data.distance =
                    (ring_mode_active == RING_CONTROL_SENSOR_BIAS) ? 1 : 0;
                ring_data.gyro_flat = 0;
                ring_state = RING_STATE_ENTRY;
            }
            else if (timeadd(&ring_data.time_l, 300))
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
            }
        }
        return 0;
    }

    switch (ring_state)
    {
    case RING_STATE_ENTRY:
        ring_data.diff_set = 0;
        if (ring_mode_active == RING_CONTROL_LEGACY)
        {
            ring_data.distance = 1;
            ring_data.yaw_delta_sum = 0;
            if (ring_data.encoder >= app.ring.ring_entry_encoder)
            {
                ring_data.encoder = 0;
                ring_data.last_yaw = 0;
                ring_data.gyro_flat = 1;
                ring_data.yaw_delta_sum = 0;
                ring_state = RING_STATE_PRE_RING;
            }
        }
        else if (ring_data.encoder >= RING_ACTIVE_PROFILE.entry_straight_encoder)
        {
            /* 直走距离不计入进环和结束判定，PRE_RING从独立零点开始积分。 */
            ring_data.encoder = 0;
            ring_data.yaw_delta_sum = 0;
            ring_data.distance = 1;
            ring_data.gyro_flat = 1;
            ring_state = RING_STATE_PRE_RING;
        }
        break;

    case RING_STATE_PRE_RING:
        if (ring_mode_active == RING_CONTROL_LEGACY)
        {
            ring_data.diff_set = app.ring.pre_ring_Gyro_target * ring_dir;
            if (ring_data.yaw_delta_sum >= app.ring.pre_ring_Gyroz)
            {
                ring_data.diff_set = 0;
                ring_state = RING_STATE_IN_RING;
            }
        }
        else if (ring_data.yaw_delta_sum >= RING_ACTIVE_PROFILE.bias_entry_yaw &&
                 ring_data.encoder >= RING_ACTIVE_PROFILE.bias_entry_encoder)
        {
            ring_data.gyro_flat = 0;
            ring_state = RING_STATE_IN_RING;
        }
        break;

    case RING_STATE_IN_RING:
        if (ring_mode_active == RING_CONTROL_LEGACY)
        {
            if (ring_data.yaw_delta_sum >= app.ring.in_ring_Gyroz &&
                ring_data.encoder >= app.ring.in_ring_encoder)
            {
                ring_data.distance = 0;
                ring_state = RING_STATE_PRE_OUT_RING;
            }
        }
        else if (ring_data.encoder >= RING_ACTIVE_PROFILE.bias_finish_encoder)
        {
            ring_data.distance = 0;
            timedestroy(&ring_data.out_ring_time);
            ring_state = RING_STATE_OUT_RING;
        }
        break;

    case RING_STATE_PRE_OUT_RING:
        if (ring_mode_active != RING_CONTROL_LEGACY)
        {
            a_run_ring_reset();
            break;
        }
        ring_data.diff_set = app.ring.pre_out_ring_Gyro_target * ring_dir;
        if (ring_data.yaw_delta_sum >= app.ring.pre_out_ring_Gyroz)
        {
            ring_data.diff_set = -5 * ring_dir;
            ring_data.encoder = 0;
            ring_data.distance = 1;
            ring_state = RING_STATE_DRIVE_OUT_RING;
        }
        break;

    case RING_STATE_DRIVE_OUT_RING:
        if (ring_mode_active != RING_CONTROL_LEGACY)
        {
            a_run_ring_reset();
            break;
        }
        ring_data.diff_set = -5 * ring_dir;
        if (ring_data.encoder >= app.ring.drive_out_ring_encoder &&
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
            a_run_ring_reset();
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
 * @brief 更新圆环编码器里程与gyro_z绝对转角积分。
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
        /* 0.012f由2ms周期和轮径/编码器标定共同确定，结果单位为cm。 */
        ring_data.encoder += (speed_l + speed_r) * 0.5f * 0.012f;
    }
}
