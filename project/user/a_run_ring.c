/**
 * @file a_run_ring.c
 * @brief 左右圆环入口识别、运行时算法选择与环内控制量维护。
 */
#include "zf_common_headfile.h"
#include "a_run_ring.h"

#define RING_ENTRY_CONFIRM_COUNT 5u     /* 300ms窗口累计命中次数，2ms调用下最快约 10ms。 */
#define RING_GAIN_REFERENCE_SPEED 50.0f /* 基础进环增益对应的目标速度。 */
#define RING_YAW_DT_SCALE 0.40f         /* gyro_z已缩放0.005，二者相乘等效 2ms 角度积分。 */
#define RING_SPEED_RELEASE_STEP 0.1f    /* 出环后每个 2ms 周期的阶梯加速步长，与圆桶保持一致。 */

static RingState ring_state = RING_STATE_IDLE; /**< 当前圆环状态机阶段，左右圆环共用。 */
RingStruct ring_data = {0};                    /**< 圆环方向、积分量和软定时器。 */
static uint8 ring_entry_count = 0;             /**< 300ms窗口内累计入口命中次数。 */
static float ring_entry_gain_active = 0.0f;    /**< 本圈入口确认时锁存的实际进环增益。 */
static float ring_ramp_speed = 0.0f;           /**< 出环释放阶段当前目标速度，逐拍爬回 speed_run。 */

#define RING_ACTIVE_PROFILE (app.ring.profile)

/**
 * @brief 根据锁存算法和圆环阶段更新角速度目标。
 * @param angle_target 指向当前角速度目标。
 */
void a_run_ring_update_angle_target(float *angle_target)
{
    if (ring_state == RING_STATE_ENTRY)
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
    if (ring_state == RING_STATE_PRE_RING ||
        ring_state == RING_STATE_IN_RING ||
        ring_state == RING_STATE_OUT_RING)
    {
        *a_value = RING_ACTIVE_PROFILE.adc_a_1;
        *b_value = RING_ACTIVE_PROFILE.adc_b_1;
        *c_value = RING_ACTIVE_PROFILE.adc_c_l;
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
    if (ring_state == RING_STATE_PRE_RING ||
        ring_state == RING_STATE_IN_RING ||
        ring_state == RING_STATE_OUT_RING)
    {
        *kp = RING_ACTIVE_PROFILE.kp_Err;
        *kd = RING_ACTIVE_PROFILE.kd_Err;
        *kp2 = RING_ACTIVE_PROFILE.kp2_Err;
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

/**
 * @brief 新圆环运行阶段使用锁存参数组的目标速度。
 *
 * RELEASE 态不覆盖，速度由 a_run_ring_update_release_speed() 阶梯接管。
 * @param speed 当前控制链目标速度指针。
 */
void a_run_ring_apply_speed(float *speed)
{
    if (ring_state == RING_STATE_PRE_RING ||
        ring_state == RING_STATE_IN_RING ||
        ring_state == RING_STATE_OUT_RING)
    {
        *speed = RING_ACTIVE_PROFILE.target_speed;
    }
}

/**
 * @brief 更新圆环出环后的后台阶梯加速。
 *
 * 出环后速度从 target_speed 逐拍加步长爬回 speed_run，避免环内降速
 * 运行时出环瞬间扭矩突变丢线；释放完成后复位状态机。
 *
 * @param speed 当前目标速度指针，非 RELEASE 态不修改。
 */
void a_run_ring_update_release_speed(float *speed)
{
    float target_speed;

    if (ring_state != RING_STATE_RELEASE)
    {
        return;
    }

    target_speed = app.speed.speed_run;
    if (ring_ramp_speed >= target_speed)
    {
        *speed = target_speed;
        a_run_ring_reset();
        return;
    }

    ring_ramp_speed += RING_SPEED_RELEASE_STEP;
    if (ring_ramp_speed > target_speed)
    {
        ring_ramp_speed = target_speed;
    }
    *speed = ring_ramp_speed;

    if (ring_ramp_speed >= target_speed)
    {
        a_run_ring_reset();
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
    if (ring_state == RING_STATE_PRE_RING)
    {
        if (ring_data.flast_l != 0)
        {
            *left_signal *= ring_entry_gain_active;
            *left_middle_signal *= ring_entry_gain_active;
        }
        else if (ring_data.flast_r != 0)
        {
            *right_middle_signal *= ring_entry_gain_active;
            *right_signal *= ring_entry_gain_active;
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
    ring_entry_gain_active = 0.0f;
    ring_ramp_speed = 0.0f;
    ring_state = RING_STATE_IDLE;
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

/**
 * @brief 按2ms主控制周期更新圆环状态机。
 * @details 完整链路：积分更新、IDLE入口计数确认、ENTRY直走、PRE_RING预入环、
 * IN_RING双条件出环、OUT_RING定时收尾，一次读完无需跳转。
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

    /* RELEASE 态与 IDLE 同等开放入口检测，序列连续两个圆环时释放中也能重进环。 */
    if (ring_state == RING_STATE_IDLE || ring_state == RING_STATE_RELEASE)
    {
        /* 五路电感阈值同时命中为圆环入口特征，300ms窗口内累计确认。 */
        if (ad1 > 30 &&
            ad2 > 5 &&
            ad3 > 5 &&
            ad4 > 30 &&
            ad5 > 20)
        {
            ring_entry_count++;
        }

        if (ring_entry_count > 0)
        {
            if (ring_entry_count >= RING_ENTRY_CONFIRM_COUNT)
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
                ring_entry_gain_active = RING_ACTIVE_PROFILE.bias_entry_gain;
                /* 以速度50为基准，斜率由菜单调节并在本圈入口锁存。 */
                ring_entry_gain_active +=
                    (RING_ACTIVE_PROFILE.target_speed - RING_GAIN_REFERENCE_SPEED) *
                    app.ring.gain_speed_slope;
                ring_data.flast_l = (ring_dir > 0) ? 1 : 0;
                ring_data.flast_r = (ring_dir < 0) ? 1 : 0;
                ring_data.diff_set = 0;
                ring_data.encoder = 0;
                ring_data.yaw_delta_sum = 0;
                ring_data.distance = 1;
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

    /* 非IDLE阶段推进电感偏置圆环状态机。 */
    switch (ring_state)
    {
    case RING_STATE_ENTRY:
        ring_data.diff_set = 0;
        if (ring_data.encoder >= RING_ACTIVE_PROFILE.entry_straight_encoder)
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
        if (ring_data.yaw_delta_sum >= RING_ACTIVE_PROFILE.bias_entry_yaw &&
            ring_data.encoder >= RING_ACTIVE_PROFILE.bias_entry_encoder)
        {
            /* 保持角度积分继续累计，供IN_RING阶段满圈出环判定使用。 */
            ring_state = RING_STATE_IN_RING;
        }
        break;

    case RING_STATE_IN_RING:
        /* 里程与满圈角度积分双条件确认，角度阈值由菜单调节，防止里程单独误判提前出环。 */
        if (ring_data.encoder >= RING_ACTIVE_PROFILE.bias_finish_encoder &&
            ring_data.yaw_delta_sum >= RING_ACTIVE_PROFILE.bias_finish_yaw)
        {
            ring_data.distance = 0;
            ring_data.gyro_flat = 0;
            timedestroy(&ring_data.out_ring_time);
            ring_state = RING_STATE_OUT_RING;
        }
        break;

    case RING_STATE_OUT_RING:
        if (timeadd(&ring_data.out_ring_time, 500))
        {
            /*
             * 环速不低于巡线速度时出环无需加速过渡，直接复位；
             * 否则进 RELEASE 态后台阶梯爬回 speed_run，释放完成才复位。
             */
            if (RING_ACTIVE_PROFILE.target_speed >= app.speed.speed_run)
            {
                a_run_ring_reset();
            }
            else
            {
                ring_ramp_speed = RING_ACTIVE_PROFILE.target_speed;
                ring_state = RING_STATE_RELEASE;
            }
            return 1;
        }
        break;

    case RING_STATE_RELEASE:
        /* 入口检测已在函数头部覆盖，此处仅作分支占位，速度由后台释放函数接管。 */
        break;

    default:
        a_run_ring_reset();
        break;
    }

    return 0;
}
