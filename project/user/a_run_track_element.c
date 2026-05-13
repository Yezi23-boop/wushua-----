/**
 * @file a_run_track_element.c
 * @brief 环岛、圆桶、跷跷板与墙面赛道元素仲裁状态机
 * @details
 * 本模块按左圆环->圆桶->跷跷板->墙面->左圆环的顺序串行开放元素识别，
 * 避免圆桶和墙面过渡段误触发下一次圆环。5ms 主控制链路直接调用本模块，
 * 减少高频路径中的只转发包装。
 */
#include "zf_common_headfile.h"

static void circle_check_l(uint8 allow_entry);

/* --- 圆环入口参数（run_time_1 以 5ms 调用） --- */
#define RING_ENTRY_CONFIRM_COUNT 3u /* 左环入口连续确认次数，5ms 调用下约 15ms。 */

/* --- 赛道元素仲裁、圆桶与墙面状态参数（run_time_1 以 5ms 调用） --- */
#define CYLINDER_AD_BOTH_HIGH_THRESHOLD 60   /* 圆桶双路强信号阈值：ad1/ad4 同时超过该值算一次命中。 */
#define CYLINDER_AD_SINGLE_HIGH_THRESHOLD 80 /* 圆桶单路强信号阈值：ad1 或 ad4 任一路超过该值也算一次命中。 */
#define CYLINDER_AD_VERTICAL_HIGH_THRESHOLD 80 /* 圆桶纵向强信号阈值：ad2 或 ad3 任一路超过该值也算一次命中。 */
#define CYLINDER_TOP_WINDOW_COUNT 100u   /* 圆桶命中统计窗口，5ms * 100 = 500ms。 */
#define CYLINDER_TOP_HIT_COUNT 3       /* 500ms 窗口内横向强信号达到该次数才认定进入圆桶段。 */
#define CYLINDER_GROUND_CONFIRM_COUNT 3u /* 5ms * 3 = 15ms，强信号消失后连续确认回地。 */
#define CYLINDER_STABLE_DELAY_COUNT 100u  /* 5ms * 100 = 500ms，回地稳定后切入跷跷板/墙面流程。 */
#define WALL_AD_SIDE_THRESHOLD 35        /* 墙面横向有效阈值，ad1/ad4 同时超过才允许推进墙面波形。 */
#define WALL_AD_HIGH_THRESHOLD 55        /* 墙面纵向高值阈值，ad2/ad3 任一路超过该值认为到达上墙峰值。 */
#define WALL_TIMING_COUNT 200u           /* 墙面强信号确认后的下墙计时，5ms * 200 = 1000ms。 */

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
    ELEMENT_NONE = TRACK_ELEMENT_NONE,             /**< 无特殊元素；保留给后续模式切换或保护降级。 */
    ELEMENT_LEFT_RING = TRACK_ELEMENT_LEFT_RING,   /**< 左圆环流程，当前已接入左环->圆桶->跷跷板->墙面串行仲裁。 */
    ELEMENT_RIGHT_RING = TRACK_ELEMENT_RIGHT_RING, /**< 右圆环流程预留位，后续左右圆环区分时直接接入。 */
    ELEMENT_CYLINDER = TRACK_ELEMENT_CYLINDER,     /**< 圆桶流程，保持菜单显示值 3 不变。 */
    ELEMENT_WALL = TRACK_ELEMENT_WALL,             /**< 墙面流程，保持菜单显示值 4 不变。 */
    ELEMENT_SEESAW = TRACK_ELEMENT_SEESAW          /**< 跷跷板流程，复用 a_run_fly 的弱磁/恢复状态机。 */
};

enum CylinderStep
{
    CYL_IDLE = 0,
    CYL_WAIT_TOP = 1,
    CYL_WAIT_GROUND = 2,
    CYL_STABLE_DELAY = 3
};

enum WallStep
{
    WALL_IDLE = 0,
    WALL_WAIT_SIGNAL = 1,
    WALL_TIMING = 2
};

// 当前环岛状态机状态，由 `circle_check_l`（5ms 主环）写入，其他模块只读。
static enum RingStep current_state = no_ring;

// 环岛过程数据由状态机写入，菜单和调试界面允许直接读取。
RingStruct ring_data = {0};
static uint8 ring_entry_count = 0;                             /**< 左环入口连续确认计数，由 `circle_check_l` 在 5ms 上下文递增。 */
static uint8 ring_finish_event = 0;                            /**< 环岛完成事件标志，由 `circle_check_l` 置位，由 `ring_take_finish_event` 消费。 */
static uint16 ring_last_ad1 = 0;                               /**< 上一轮 5ms 仲裁使用的 ad1 快照，用于过滤非上升沿入口误判。 */
static uint16 ring_last_ad2 = 0;                               /**< 上一轮 5ms 仲裁使用的 ad2 快照，用于过滤非上升沿入口误判。 */
static uint16 ring_last_ad3 = 0;                               /**< 上一轮 5ms 仲裁使用的 ad3 快照，用于过滤非上升沿入口误判。 */
static uint16 ring_last_ad4 = 0;                               /**< 上一轮 5ms 仲裁使用的 ad4 快照，用于过滤非上升沿入口误判。 */
static uint8 ring_adc_history_valid = 0;                       /**< 电感历史是否已有有效快照；上电首拍不允许作为上升沿。 */
static uint8 ring_adc_rising = 0;                              /**< 当前 5ms 周期四路电感是否都相对上一拍严格上升。 */
static enum TrackElement expected_element = ELEMENT_LEFT_RING; /**< 当前期望赛道元素，用于串行屏蔽非当前元素的入口识别。 */
static enum CylinderStep cylinder_state = CYL_IDLE;            /**< 圆桶状态机阶段，由 `cylinder_update_5ms` 在 5ms 上下文推进。 */
static uint8 cylinder_top_count = 0;                           /**< 圆桶窗口内命中次数，达到阈值后认定进入圆桶段。 */
static uint8 cylinder_top_window_count = 0;                    /**< 圆桶命中统计窗口计数，首个强信号后开始计时，最大 500ms。 */
static uint8 cylinder_ground_count = 0;                        /**< 圆桶回地确认计数，横向强信号连续消失后才认定回地。 */
static uint8 cylinder_stable_count = 0;                        /**< 圆桶回地稳定延时计数，100 次（500ms）后进入下一元素。 */
static float cylinder_vz = 0.0f;                               /**< 圆桶状态机当前使用的 5ms roll 角差，单位：度，直接来自 IMU 缓存。 */
static enum WallStep wall_state = WALL_IDLE;                   /**< 墙面状态机阶段，圆桶完成后由 5ms 主环推进。 */
static uint16 wall_timer_count = 0;                            /**< 墙面完整波形确认后的下墙计时，单位：5ms。 */

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
    if (ad1 > 35 &&
        ad2 > 10 &&
        ad3 > 10 &&
        ad4 > 35 &&
        ad1 < 80 &&
        ad2 < 60 &&
        ad3 < 60 &&
        ad4 < 80)
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
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶，4-墙面，5-跷跷板。
 */
int8 a_run_track_element_get_expected_element(void)
{
    return (int8)expected_element;
}

/**
 * @brief 读取当前圆桶状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_track_element_get_cylinder_state(void)
{
    return (int8)cylinder_state;
}

/**
 * @brief 读取当前墙面状态机阶段。
 * @return int8 0-空闲，1-等墙面强信号，2-下墙计时。
 */
int8 a_run_track_element_get_wall_state(void)
{
    return (int8)wall_state;
}

/**
 * @brief 读取圆桶判断当前使用的 roll 角差。
 * @return float 5ms IMU 缓存的 roll 角差，单位：度，范围 -180~180。
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
 * 圆桶流程只维护本地状态计数，负压输出保持固定百分比链路。
 */
static void cylinder_reset_state(void)
{
    cylinder_top_count = 0;
    cylinder_top_window_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_IDLE;
}

/**
 * @brief 复位墙面状态机。
 *
 * 墙面只作为圆桶后的屏蔽/确认段，复位时清掉计时状态，
 * 避免下一轮从上一次下墙等待中继续运行。
 */
static void wall_reset_state(void)
{
    wall_state = WALL_IDLE;
    wall_timer_count = 0;
}

/**
 * @brief 启动墙面强信号等待阶段。
 *
 * 圆桶完成后进入墙面流程，只等待横向有效且纵向任一路达到高值，
 * 优先保证能屏蔽圆桶后的过渡段，避免过严波形条件导致一直识别不到墙面。
 */
static void wall_start_wait_signal(void)
{
    wall_state = WALL_WAIT_SIGNAL;
    wall_timer_count = 0;
}

/**
 * @brief 启动圆桶等待过顶阶段。
 *
 * 左环完成后进入圆桶流程时清空所有确认计数，使顶部/回地判断只依赖当前圆桶段数据。
 */
static void cylinder_start_wait_top(void)
{
    cylinder_top_count = 0;
    cylinder_top_window_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_WAIT_TOP;
}

/**
 * @brief 更新圆桶过顶/回地状态机。
 * @return uint8 1-圆桶流程完成，可切入墙面流程；0-仍在圆桶流程中。
 * @details
 * 该函数由 5ms 主控制链路调用，使用 ad1/ad4 在固定时间窗口内的命中次数判断圆桶段。
 * 圆桶确认、回地和稳定延时都通过计数去抖，避免单次电感尖峰触发状态跳变。
 */
static uint8 cylinder_update_5ms(void)
{
    uint8 cylinder_ad_high;

    if (cylinder_state == CYL_IDLE)
    {
        cylinder_start_wait_top();
    }

    cylinder_ad_high = 0;
    if ((ad1 > CYLINDER_AD_BOTH_HIGH_THRESHOLD &&
         ad4 > CYLINDER_AD_BOTH_HIGH_THRESHOLD) ||
        ad1 > CYLINDER_AD_SINGLE_HIGH_THRESHOLD ||
        ad4 > CYLINDER_AD_SINGLE_HIGH_THRESHOLD ||
        ad2 > CYLINDER_AD_VERTICAL_HIGH_THRESHOLD ||
        ad3 > CYLINDER_AD_VERTICAL_HIGH_THRESHOLD)
    {
        cylinder_ad_high = 1;
    }

    switch (cylinder_state)
    {
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
            cylinder_stable_count = 0;
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
 * @brief 更新墙面识别/下墙计时状态机。
 * @return uint8 1-墙面流程完成，可重新开放左圆环；0-仍在墙面流程中。
 * @details
 * 墙面位于圆桶之后，使用横向有效和纵向高值确认墙面强信号。
 * 确认后只做 5ms 计数等待下墙，不在该阶段开放圆环入口识别。
 */
static uint8 wall_update_5ms(void)
{
    uint16 current_ad2;
    uint16 current_ad3;
    uint8 side_valid;
    uint8 high_valid;
    uint8 wall_done;

    if (wall_state == WALL_IDLE)
    {
        wall_start_wait_signal();
    }

    current_ad2 = (uint16)ad2;
    current_ad3 = (uint16)ad3;
    side_valid = 0;
    high_valid = 0;
    wall_done = 0;

    if (ad1 > WALL_AD_SIDE_THRESHOLD && ad4 > WALL_AD_SIDE_THRESHOLD)
    {
        side_valid = 1;
    }
    if (current_ad2 > WALL_AD_HIGH_THRESHOLD || current_ad3 > WALL_AD_HIGH_THRESHOLD)
    {
        high_valid = 1;
    }

    switch (wall_state)
    {
    case WALL_WAIT_SIGNAL:
        if (side_valid != 0 && high_valid != 0)
        {
            wall_timer_count = 0;
            wall_state = WALL_TIMING;
        }
        break;

    case WALL_TIMING:
        wall_timer_count++;
        if (wall_timer_count >= WALL_TIMING_COUNT)
        {
            wall_done = 1;
            wall_reset_state();
        }
        break;

    default:
        wall_reset_state();
        break;
    }

    return wall_done;
}

/**
 * @brief 复位赛道元素仲裁状态机。
 *
 * 菜单关闭圆环识别时，仲裁、环岛、圆桶和墙面必须同步回到初始状态，
 * 否则重新开启时可能从上一次的中间阶段继续运行。
 */
static void track_element_reset_state(void)
{
    expected_element = ELEMENT_LEFT_RING;
    ring_last_ad1 = 0;
    ring_last_ad2 = 0;
    ring_last_ad3 = 0;
    ring_last_ad4 = 0;
    ring_adc_history_valid = 0;
    ring_adc_rising = 0;
    ring_reset_state();
    cylinder_reset_state();
    wall_reset_state();
    a_run_fly_reset();
}

/**
 * @brief 更新赛道元素仲裁状态机。
 *
 * 5ms 调用，根据 `expected_element` 当前期望元素开放左圆环、圆桶、跷跷板或墙面流程。
 * 左环入口会使用上一拍电感快照确认四路同步上升，之后再进入连续阈值确认。
 * 各元素串行开放：左环完成后进入圆桶，圆桶完成后按飞坡开关进入跷跷板或墙面，
 * 跷跷板恢复完成后进入墙面，墙面计时完成后才重新开放下一次左环入口。
 *
 * @note 由 5ms 主控制环调用，函数内部刷新圆桶姿态快照并推进元素仲裁状态迁移。
 */
void a_run_track_element_update_gate(void)
{
    uint8 cylinder_done;
    uint8 seesaw_done;
    uint8 wall_done;

    cylinder_vz = imu_get_gravity_vz();
    if (ring_adc_history_valid != 0 &&
        ring_last_ad1 < ad1 &&
        ring_last_ad2 < ad2 &&
        ring_last_ad3 < ad3 &&
        ring_last_ad4 < ad4)
    {
        ring_adc_rising = 1;
    }
    else
    {
        ring_adc_rising = 0;
    }
    ring_last_ad1 = ad1;
    ring_last_ad2 = ad2;
    ring_last_ad3 = ad3;
    ring_last_ad4 = ad4;
    ring_adc_history_valid = 1;

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
            if (app.fly.fly_ramp_enable == 1)
            {
                a_run_fly_reset();
                expected_element = ELEMENT_SEESAW;
            }
            else
            {
                expected_element = ELEMENT_WALL;
                wall_start_wait_signal();
            }
        }
        return;
    }

    if (expected_element == ELEMENT_SEESAW)
    {
        circle_check_l(0);
        (void)ring_take_finish_event();

        if (app.fly.fly_ramp_enable != 1)
        {
            a_run_fly_reset();
            expected_element = ELEMENT_WALL;
            wall_start_wait_signal();
            return;
        }

        seesaw_done = a_run_fly_take_finish_event();
        if (seesaw_done != 0)
        {
            expected_element = ELEMENT_WALL;
            wall_start_wait_signal();
        }
        return;
    }

    if (expected_element == ELEMENT_WALL)
    {
        circle_check_l(0);
        (void)ring_take_finish_event();
        wall_done = wall_update_5ms();
        if (wall_done != 0)
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
 * 入口确认的第一拍必须四路电感同步上升，后续连续确认只看入口阈值，
 * 避免平台段不再上升时打断已经开始的进环确认。
 *
 * @param allow_entry 1-允许入口识别，0-只维持/清理已有环岛流程。
 */
static void circle_check_l(uint8 allow_entry)
{
    int8 entry_signal;

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
        entry_signal = ring_is_left_entry_signal();
        /* 第一拍加上升沿约束，后续保持原阈值连续确认，避免入口平台段漏判。 */
        if (allow_entry != 0 &&
            entry_signal != 0 &&
            (ring_entry_count != 0 || ring_adc_rising != 0))
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
            /* 在 200ms 窗口内达到连续确认次数，判定左环成立。 */
            if (ring_entry_count >= RING_ENTRY_CONFIRM_COUNT)
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l); // 清空左环识别定时器
                ring_data.flast_l = 1;          // 置位左环过程标志
                /* 从入口识别切到 ring，后续进入距离累计阶段 */
                current_state = ring; // 切换到环岛准备阶段
            }
            /* 超过 200ms 仍未满足次数，丢弃本次识别 */
            else if (timeadd(&ring_data.time_l, 200))
            {
                ring_entry_count = 0;
                timedestroy(&ring_data.time_l);
            }
        }
        break;

    case ring:
//		stop=1;
        /* 入环前先恢复普通循迹角速度目标，开始累计入口距离。 */
        ring_data.diff_set = 0;
        ring_data.distance = 1; // 允许累计编码器里程
        ring_data.yaw_delta_sum = 0;

        /* 累计距离达到阈值后进入预入环阶段 */
        if (ring_data.encoder >= app.ring.ring_entry_encoder)
        {
            ring_data.distance = 0;
            ring_data.encoder = 0;
            ring_data.last_yaw = 0; // 当前使用 gyro_z 绝对积分，不再依赖欧拉 yaw 起点。
            ring_data.gyro_flat = 1;
            ring_data.yaw_delta_sum = 0;
            current_state = pre_ring; // 切换到预入环阶段
        }
        break;

    case pre_ring:
        /* 控制环 gyro_z 已统一为左转正；左环固定角速度目标保持正值，便于现场调参。 */
        ring_data.diff_set = app.ring.pre_ring_Gyro_target;

        /* gyro_z 绝对积分只表示已转过的角度大小，不再依赖 IMU 欧拉 yaw。 */
        if (ring_data.yaw_delta_sum >= app.ring.pre_ring_Gyroz )
        {
            ring_data.diff_set = 0;
            current_state = in_ring;
        }
        break;

    case in_ring:
        if (ring_data.yaw_delta_sum >= app.ring.in_ring_Gyroz)
        {
            current_state = pre_out_ring;

        }
        break;

    case pre_out_ring:
        /* 预出环继续按左环方向给正角速度目标，角度累计只判断转过的大小。 */
        ring_data.diff_set = app.ring.pre_out_ring_Gyro_target;

        /* 预出环继续沿左环方向打到更大的出环角度，避免过早回线导致压线不稳。 */
        if (ring_data.yaw_delta_sum >= app.ring.pre_out_ring_Gyroz)
        {
            ring_data.diff_set = 0;
            ring_data.gyro_flat = 0;
            ring_data.yaw_delta_sum = 0;
            timedestroy(&ring_data.out_ring_time);
            current_state = out_ring;
        }
        break;

    case out_ring:
        if (timeadd(&ring_data.out_ring_time, 600))
        {
//			stop=1;
            timedestroy(&ring_data.out_ring_time);
            ring_data.flast_l = 0;                 // 清除左环过程标志
            ring_data.last_yaw = 0;                // 清除历史 yaw 方案保留字段
            ring_data.diff_set = 0;                // 清零环岛目标角速度覆盖
            ring_data.distance = 0;                // 关闭里程累计
            ring_data.encoder = 0;                 // 清零里程累计量
            ring_data.gyro_flat = 0;               // 关闭 gyro_z 角度累计
            ring_data.yaw_delta_sum = 0;           // 清零 gyro_z 绝对角增量
            ring_finish_event = 1;
            current_state = no_ring;               // 返回普通巡线状态
        }
        break;
    }
}

/**
 * @brief 更新环岛判定所需的里程与转角量。
 * @details
 * `encoder` 继续按速度估计累计里程；`yaw_delta_sum` 使用 `gyro_z` 的绝对值积分。
 * `gyro_z` 已在 IMU 桥接层按 5ms 周期缩放为单周期角度增量，这里直接累加即可。
 *
 * @note 由 5ms 主控制链路调用；若 IMU 桥接层缩放或调用周期改变，圆环角度阈值需同步校准。
 */
void a_run_track_element_update_integrals(void)
{
    float delta_angle;

    /* 用 gyro_z 做单周期角度增量，避开墙面后欧拉 yaw 不连续导致的第二圈卡环。 */
    if (ring_data.gyro_flat == 1)
    {
        delta_angle = gyro_z;
        if (delta_angle < 0.0f)
        {
            delta_angle = -delta_angle;
        }

        ring_data.yaw_delta_sum += delta_angle;
    }

    /* 编码器累计使能时，累加当前前进距离估计 */
    if (ring_data.distance == 1)
    {
        /* 0.01 系数与当前速度单位配套，保持里程判据量级稳定 */
        ring_data.encoder += (speed_l + speed_r) * 0.005; // 用于判断是否达到环岛距离阈值
    }
}
