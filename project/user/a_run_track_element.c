/**
 * @file a_run_track_element.c
 * @brief 环岛、圆桶与赛道元素仲裁状态机
 * @details
 * 本模块收敛左圆环、圆桶和后续右圆环扩展入口。对外仍由 a_run_mode.c
 * 统一调配，保证主控制任务只依赖稳定的运行模式接口。
 */
#include "zf_common_headfile.h"

static void circle_check_l(uint8 allow_entry);

/* --- 圆环入口参数（run_time_1 以 5ms 调用） --- */
#define RING_ENTRY_CONFIRM_COUNT 3u /* 左环入口连续确认次数，5ms 调用下约 15ms */

/* --- 赛道元素仲裁与圆筒状态参数（run_time_1 以 5ms 调用） --- */
#define CYLINDER_TOP_GRAVITY_Z -0.3f     /* vz 到达该值以下，认为接近圆桶顶部，单位：g。 */
#define CYLINDER_GROUND_GRAVITY_Z 0.3f   /* vz 回到该值以上，认为车身已回地，单位：g。 */
#define CYLINDER_TOP_CONFIRM_COUNT 3u    /* 5ms * 3 = 15ms，抑制单次冲击误判。 */
#define CYLINDER_GROUND_CONFIRM_COUNT 3u /* 5ms * 3 = 15ms，回地同样做连续确认。 */
#define CYLINDER_STABLE_DELAY_COUNT 50u  /* 5ms * 50 = 250ms，回地稳定后再恢复圆环识别。 */

/**
 * @brief 环岛阶段枚举。
 * @details 描述左环识别、入环、环内和出环的各个状态。
 */
enum RingStep
{
    no_ring,      // 未进入环岛流程
    ring,         // 已识别到左环入口
    pre_ring,     // 预入环阶段
    in_ring,      // 环内阶段
    pre_out_ring, // 预出环阶段
    out_ring      // 出环确认阶段
};

enum TrackElement
{
    ELEMENT_NONE = 0,       /**< 无特殊元素；保留给后续模式切换或保护降级。 */
    ELEMENT_LEFT_RING = 1,  /**< 左圆环流程，当前已接入左环->圆桶串行仲裁。 */
    ELEMENT_RIGHT_RING = 2, /**< 右圆环流程预留位，后续左右圆环区分时直接接入。 */
    ELEMENT_CYLINDER = 3    /**< 圆桶流程，保持菜单显示值 3 不变。 */
};

enum CylinderStep
{
    CYL_IDLE = 0,
    CYL_WAIT_TOP = 1,
    CYL_WAIT_GROUND = 2,
    CYL_STABLE_DELAY = 3
};

// 当前环岛状态机状态，由 `circle_check_l`（5ms 主环）写入，其他模块只读。
static enum RingStep current_state = no_ring;

// 环岛过程数据由状态机写入，菜单和调试界面允许直接读取。
RingStruct ring_data = {0};
static uint8 ring_entry_count = 0;                             /**< 左环入口连续确认计数，由 `circle_check_l` 在 5ms 上下文递增。 */
static uint8 ring_finish_event = 0;                            /**< 环岛完成事件标志，由 `circle_check_l` 置位，由 `ring_take_finish_event` 消费。 */
static enum TrackElement expected_element = ELEMENT_LEFT_RING; /**< 当前期望赛道元素，预留左右圆环与圆桶的分流位置。 */
static enum CylinderStep cylinder_state = CYL_IDLE;            /**< 圆桶状态机阶段，由 `cylinder_update_5ms` 在 5ms 上下文推进。 */
static uint8 cylinder_top_count = 0;                           /**< 圆桶顶部确认计数，连续 3 次（15ms）vz 低于阈值才认定过顶。 */
static uint8 cylinder_ground_count = 0;                        /**< 圆桶回地确认计数，连续 3 次（15ms）vz 高于阈值才认定回地。 */
static uint8 cylinder_stable_count = 0;                        /**< 圆桶回地稳定延时计数，50 次（250ms）后才允许恢复圆环识别。 */
static float cylinder_vz = 1.0f;                               /**< 圆桶状态机当前使用的 5ms 重力向量 Z 分量，直接来自 IMU 缓存。 */

/**
 * @brief 根据环岛状态更新角速度目标。
 *
 * 仅在环岛阶段 `ring_data.diff_set` 非零时覆盖目标角速度，实现环内固定转向。
 * 出环后 `diff_set` 清零，控制链路自动恢复普通循迹角速度外环。
 *
 * @param angle_target 指向目标角速度的指针，由调用方提供上下文。
 *
 * @note 由 5ms 主控制环调用，避免在高频路径中引入额外分支判断。
 */
void a_run_track_element_update_angle_target(float *angle_target)
{
    if (ring_data.diff_set != 0)
    {
        *angle_target = ring_data.diff_set;
    }
}

/**
 * @brief 判断左环入口电感特征是否命中。
 *
 * 对称翻转原右环特征：入口处横向电感 ad1/ad4 较高、竖向电感 ad2/ad3 较低。
 * 左右阈值用于在弯道内侧提前确认进入环岛，而不是等车身完全进去再判断。
 *
 * @return int8 1-命中左环入口特征，0-未命中。
 */
static int8 ring_is_left_entry_signal(void)
{
    if (ad1 > 40 &&
        ad2 > 15 &&
        ad3 > 15 &&
        ad4 > 40 &&
        ad1 < 70 &&
        ad2 < 40 &&
        ad3 < 40 &&
        ad4 < 70)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 读取当前环岛状态机阶段。
 * @return int8 当前阶段编号：0-no_ring，1-ring，2-pre_ring，3-in_ring，4-pre_out_ring，5-out_ring。
 *
 * @note 仅供菜单和调试显示读取，不应由外部模块直接驱动状态迁移。
 */
int8 a_run_track_element_get_ring_state(void)
{
    return (int8)current_state;
}

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶。
 */
int8 a_run_track_element_get_expected_element(void)
{
    return (int8)expected_element;
}

/**
 * @brief 读取当前圆筒状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_track_element_get_cylinder_state(void)
{
    return (int8)cylinder_state;
}

/**
 * @brief 读取圆桶判断当前使用的重力向量 vz。
 * @return float 5ms IMU 缓存的 vz，已在 IMU 模块限幅。
 */
float a_run_track_element_get_cylinder_vz(void)
{
    return cylinder_vz;
}

/**
 * @brief 取出并清除环岛完成事件。
 *
 * 环岛完成只允许仲裁状态机消费一次，避免圆桶流程被同一次出环事件重复触发。
 *
 * @return uint8 1-存在待消费完成事件，0-无事件。
 */
static uint8 ring_take_finish_event(void)
{
    uint8 event;

    event = ring_finish_event;
    ring_finish_event = 0;

    return event;
}

/**
 * @brief 复位环岛状态机和环岛输出覆盖量。
 * @details 菜单关闭圆环时立即清掉阶段、计时和目标角速度覆盖，避免关闭后残留控制量继续影响主控链路。
 */
static void ring_reset_state(void)
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
    ring_finish_event = 0;
    current_state = no_ring;
}

/**
 * @brief 复位圆桶状态机。
 *
 * 圆桶流程会临时置位负压过顶标志，复位时必须同步恢复负压模块，
 * 避免退出仲裁后仍残留圆桶过顶状态。
 */
static void cylinder_reset_state(void)
{
    cylinder_top_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_IDLE;
    fuya_restore_cylinder_peak_angle();
}

/**
 * @brief 启动圆桶等待过顶阶段。
 *
 * 左环完成后进入圆桶流程时清空所有确认计数，使顶部/回地判断只依赖当前圆桶段数据。
 */
static void cylinder_start_wait_top(void)
{
    cylinder_top_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_WAIT_TOP;
    fuya_restore_cylinder_peak_angle();
}

/**
 * @brief 更新圆桶过顶/回地状态机。
 * @return uint8 1-圆桶流程完成，可恢复后续圆环识别；0-仍在圆桶流程中。
 * @details
 * 该函数由 5ms 主控制链路调用，直接使用当前 vz 判断顶部和回地。
 * 顶部、回地和稳定延时都通过计数去抖，避免单次冲击触发状态跳变。
 */
static uint8 cylinder_update_5ms(void)
{
    float vz;

    if (cylinder_state == CYL_IDLE)
    {
        cylinder_start_wait_top();
    }

    vz = cylinder_vz;

    switch (cylinder_state)
    {
    case CYL_WAIT_TOP:
        if (vz <= CYLINDER_TOP_GRAVITY_Z)
        {
            cylinder_top_count++;
            if (cylinder_top_count >= CYLINDER_TOP_CONFIRM_COUNT)
            {
                cylinder_top_count = 0;
                cylinder_ground_count = 0;
                cylinder_stable_count = 0;
                fuya_apply_cylinder_peak_angle();
                stop = 1;
                cylinder_state = CYL_WAIT_GROUND;
            }
        }
        else
        {
            cylinder_top_count = 0;
        }
        break;

    case CYL_WAIT_GROUND:
        if (vz >= CYLINDER_GROUND_GRAVITY_Z)
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
            cylinder_stable_count = 0;
            fuya_restore_cylinder_peak_angle();
            cylinder_state = CYL_IDLE;
            return 1;
        }
        break;

    default:
        cylinder_reset_state();
        break;
    }

    return 0;
}

/**
 * @brief 复位赛道元素仲裁状态机。
 *
 * 菜单关闭圆环识别时，仲裁、环岛和圆桶必须同步回到初始状态，
 * 否则重新开启时可能从上一次的中间阶段继续运行。
 */
static void track_element_reset_state(void)
{
    expected_element = ELEMENT_LEFT_RING;
    ring_reset_state();
    cylinder_reset_state();
}

/**
 * @brief 更新赛道元素仲裁状态机。
 *
 * 5ms 调用，根据 `expected_element` 当前期望元素开放左圆环或圆筒识别。
 * 左环与圆筒为串行流程：左环完成后进入圆筒，圆筒完成后回到左环。
 * 右环功能在代码中标记为未实现，运行时按模式 0 执行。
 *
 * @note 由 5ms 主控制环调用，函数内部刷新圆桶姿态快照并推进环岛状态迁移。
 */
void a_run_track_element_update_gate(void)
{
    uint8 cylinder_done;

    cylinder_vz = imu_get_gravity_vz();

    if (app.start.circle_flags != 1)
    {
        track_element_reset_state();
        return;
    }

    if (expected_element == ELEMENT_LEFT_RING)
    {
        circle_check_l(1);
        if (ring_take_finish_event() != 0)
        {
            expected_element = ELEMENT_CYLINDER;
            cylinder_start_wait_top();
        }
        return;
    }

    if (expected_element == ELEMENT_CYLINDER)
    {
        circle_check_l(0);
        (void)ring_take_finish_event();
        cylinder_done = cylinder_update_5ms();
        if (cylinder_done != 0)
        {
            expected_element = ELEMENT_LEFT_RING;
        }
        return;
    }

    expected_element = ELEMENT_LEFT_RING;
}

/**
 * @brief 左环状态机更新。
 * @details 根据电感特征、编码器累计和角速度累计结果推进左环流程。
 * 注意此部分为高层状态机，不涉及高频浮点解算，但条件判断需防抖。
 *
 * @param allow_entry 1-允许入口识别，0-只维持/清理已有环岛流程。
 */
static void circle_check_l(uint8 allow_entry)
{
    if (app.start.circle_flags != 1)
    {
        if (ring_entry_count != 0 || current_state != no_ring ||
            ring_data.diff_set != 0 || ring_data.distance != 0 ||
            ring_data.gyro_flat != 0 || ring_data.flast_l != 0 ||
            ring_data.flast_r != 0)
        {
            ring_reset_state();
        }
        return;
    }

    /* 状态机每次只推进一步，确保计时与传感器判定可追踪 */
    switch (current_state)
    {
    case no_ring:
        /* 1. 根据电感特征识别左环入口 (对称翻转原右环特征) */
        if (allow_entry != 0 &&
            ring_is_left_entry_signal() != 0)
        {
            ring_entry_count++;
        }
        else
        {
            ring_entry_count = 0;
            timedestroy(&ring_data.time_l);
        }

        /* 2. 在限定时间内连续命中多次才确认进入环岛 */
        if (ring_entry_count > 0)
        {
            /* 在 1000ms 窗口内达到连续确认次数，判定左环成立。 */
            if (ring_entry_count >= RING_ENTRY_CONFIRM_COUNT)
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l); // 清空左环识别定时器
                ring_data.flast_l = 1;          // 置位左环过程标志
                /* 从入口识别切到 ring，后续进入距离累计阶段 */
                current_state = ring; // 切换到环岛准备阶段
            }
            /* 超过 1000ms 仍未满足次数，丢弃本次识别 */
            else if (timeadd(&ring_data.time_l, 1000))
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
            }
        }
        break;

    case ring:
        /* 入环前先恢复普通循迹角速度目标，开始累计入口距离。 */
        ring_data.diff_set = 0;
        ring_data.distance = 1; // 允许累计编码器里程
        ring_data.yaw_delta_sum = 0;

        /* 累计距离达到阈值后进入预入环阶段 */
        if (ring_data.encoder >= app.ring.ring_entry_encoder)
        {
            ring_data.distance = 0;
            ring_data.encoder = 0;
            ring_data.last_yaw = imu660rc_yaw; // 记录预入环阶段初始航向角
            ring_data.gyro_flat = 1;
            ring_data.yaw_delta_sum = 0;
            current_state = pre_ring; // 切换到预入环阶段
        }
        break;

    case pre_ring:
        /* 控制环 gyro_z 已统一为左转正；左环固定角速度目标保持正值，便于现场调参。 */
        ring_data.diff_set = app.ring.pre_ring_Gyro_target;

        /* 相对预入环起点的偏航角累计达到入环阈值并确认 50ms 后，认为已真正入环。 */
        /* 由于累加的角度自带符号，这里直接取绝对值判断是否转够30度即可，不论左右环。 */
        if (ring_data.yaw_delta_sum < -app.ring.pre_ring_Gyroz && timeadd(&ring_data.ing_ring_time, 50))
        {
            ring_data.diff_set = 0;
            timedestroy(&ring_data.ing_ring_time);
            current_state = in_ring;
        }
        break;

    case in_ring:
        /* 左环累计 yaw 角增量为负，必须达到负向环内阈值后才进入预出环。 */
        if (ring_data.yaw_delta_sum <= -app.ring.in_ring_Gyroz)
        {
            current_state = pre_out_ring;
        }
        break;

    case pre_out_ring:
        /* 预出环继续按左环方向给正角速度目标，yaw 累计仍按顺时针正判断负角度。 */
        ring_data.diff_set = app.ring.pre_out_ring_Gyro_target;

        /* 预出环继续沿左环方向打到更大的出环角度，避免过早回线导致压线不稳。 */
        if (ring_data.yaw_delta_sum < -app.ring.pre_out_ring_Gyroz && timeadd(&ring_data.out_ring_time, 50))
        {
            ring_data.diff_set = 0;
            timedestroy(&ring_data.out_ring_time);
            ring_data.gyro_flat = 0;
            ring_data.yaw_delta_sum = 0;
            current_state = out_ring;
        }
        break;

    case out_ring:
        /* 1000ms 内电感重新平衡，则认为已完全驶离环岛 */
        if (func_abs((int)ad1 - (int)ad4) < 10 && timeadd(&ring_data.out_ring_time, 100))
        {
            timedestroy(&ring_data.out_ring_time); // 清空出环确认定时器
            ring_data.flast_l = 0;                 // 清除左环过程标志
            ring_data.last_yaw = 0;                // 清除初始航向角缓存
            ring_data.diff_set = 0;                // 清零环岛目标角速度覆盖
            ring_data.distance = 0;                // 关闭里程累计
            ring_data.encoder = 0;                 // 清零里程累计量
            ring_data.gyro_flat = 0;               // 关闭相对偏航角更新
            ring_data.yaw_delta_sum = 0;           // 清零相对偏航角差
            current_state = no_ring;               // 返回普通巡线状态
            ring_finish_event = 1;
        }
        break;
    }
}

/**
 * @brief 更新环岛判定所需的里程与偏航量。
 * @details
 * `encoder` 继续按速度估计累计里程；`yaw_delta_sum` 使用前后两次 yaw 的差值进行增量累加。
 * 这种方式可以避免跨0点跳变，并且累加出来的是实际转过的总角度量，符合转角触发阈值。
 *
 * @note 该函数依赖进入 `pre_ring` 时已经正确记录 `last_yaw`。
 */
void a_run_track_element_update_integrals(void)
{
    float delta_yaw;

    /* 环岛阶段只关心相对转过多少角，通过前后两次yaw的差值进行增量累加。 */
    if (ring_data.gyro_flat == 1)
    {
        delta_yaw = imu660rc_yaw - ring_data.last_yaw;

        /* 处理跨越0点（或360点）的情况，将角差折返到 -180~180。 */
        if (delta_yaw > 180.0f)
        {
            delta_yaw -= 360.0f;
        }
        else if (delta_yaw < -180.0f)
        {
            delta_yaw += 360.0f;
        }

        ring_data.yaw_delta_sum += delta_yaw;
        ring_data.last_yaw = imu660rc_yaw;
    }

    /* 编码器累计使能时，累加当前前进距离估计 */
    if (ring_data.distance == 1)
    {
        /* 0.01 系数与当前速度单位配套，保持里程判据量级稳定 */
        ring_data.encoder += (speed_l + speed_r) * 0.005; // 用于判断是否达到环岛距离阈值
    }
}
