/**
 * @file a_run_mode.c
 * @brief 运行模式状态机（启停、飞坡、环岛）
 * @details
 * 本模块聚合车辆运行阶段相关状态机：
 * 1) 启停状态：按键/外部命令驱动停止、预启动、运行三态切换；
 * 2) 飞坡状态：根据四路电感特征触发短时速度与转向覆盖；
 * 3) 右环状态机：基于电感特征、编码器累计、角速度累计进行分阶段切换。
 *
 * 该文件直接影响赛道特征段通过策略，注释重点说明状态切换条件与计时含义。
 */
#include "zf_common_headfile.h"
#include "a_run_mode.h"

/* --- 飞坡状态内部变量 --- */
static int fly_detect_count = 0; /* 飞坡入口连续弱磁确认计数 */
static int fly_state_count = 0;  /* 飞坡保持、恢复和冷却阶段的 5ms 计数 */

/* --- 启停状态机参数 --- */
#define START_DEBOUNCE_TIME 5 /* 启动按键消抖确认次数（10ms 调用周期下约 500ms） */

/* --- 飞坡状态机参数（run_time_1 以 5ms 调用） --- */
#define FLY_AD_SIDE_LOST_TH 14u  /* 横向电感低于该值时认为主线信号正在消失 */
#define FLY_AD_CENTER_LOST_TH 5u /* 竖向电感阈值更低，避免普通弱弯误触发飞坡 */
#define FLY_HOLD_ANGLE 0         /* 飞坡离线阶段固定目标角速度，0 表示直行锁角 */
#define FLY_RAMP_BLOCK_VZ 0.95f  /* 低于该重力 Z 分量时认为车身已明显离开平面姿态 */
#define FLY_RECOVER_COUNT 2      /* 落地恢复窗口，单位 5ms，默认约 20ms */
#define FLY_COOLDOWN_COUNT 20    /* 退出冷却窗口，单位 5ms，默认约 100ms */

/* --- 圆环姿态门控参数（run_time_2 以 10ms 调用） --- */
#define RING_FLAT_BLOCK_VZ 0.95f    /* 低于该重力 Z 分量时认为已进入桶/墙/坡面姿态 */
#define RING_FLAT_RELEASE_VZ 0.98f  /* 回到该重力 Z 分量以上才重新允许圆环识别 */
#define RING_ENTRY_CONFIRM_COUNT 3u /* 左环入口连续确认次数，10ms 调用下约 30ms */

/* --- 赛道元素仲裁与圆筒状态参数（run_time_2 以 10ms 调用） --- */
#define TRACK_MODE_LEFT_RING_CYLINDER 0
#define CYLINDER_TOP_VZ -0.85f
#define CYLINDER_GROUND_VZ 0.80f
#define CYLINDER_TOP_CONFIRM_COUNT 2u
#define CYLINDER_GROUND_CONFIRM_COUNT 2u
#define CYLINDER_STABLE_DELAY_COUNT 25u

enum StartState
{
    START_STATE_0 = 0, // 停止/待机
    START_STATE_1 = 1, // 预启动
    START_STATE_2 = 2  // 允许运行
};

static enum StartState current_start_state = START_STATE_0; // 当前启动状态
static int press_debounce_cnt = 0;                          // 按下消抖计数
static int8 key_released = 1;                               // 按键释放锁存：1-已释放，0-仍按下

/**
 * @brief 判断车身是否已经明显离开平面姿态。
 *
 * 平地丢线时四路电感也可能同时很低，因此飞坡入口不能只依赖电感。
 * 重力向量 Z 分量低于阈值时，认为车身已经进入坡面或飞坡姿态。
 *
 * @return int8 1-姿态满足飞坡触发条件，0-仍近似平面。
 */
static int8 fly_is_vzc_ramp_pose(void)
{
    float vzc;

    vzc = fuya_last_vzc;

    if (vzc < FLY_RAMP_BLOCK_VZ)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断当前电感是否满足飞坡入口弱磁特征。
 *
 * 飞坡入口通常表现为四路归一化电感同时快速跌低，并伴随重力 Z 分量下降。
 * 姿态条件用于过滤平地丢线，避免把普通弱磁或赛道断线误判为飞坡。
 *
 * @return int8 1-满足飞坡入口特征，0-不满足。
 *
 * @note 由 5ms 主控制环调用，只做常量比较，避免增加实时链路负担。
 */
static int8 fly_is_ramp_lost_signal(void)
{
    if (ad1 < FLY_AD_SIDE_LOST_TH &&
        ad2 < FLY_AD_CENTER_LOST_TH &&
        ad3 < FLY_AD_CENTER_LOST_TH &&
        ad4 < FLY_AD_SIDE_LOST_TH &&
        fly_is_vzc_ramp_pose())
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断飞坡落地恢复是否已回到中线附近。
 *
 * 中线在控制链路中对应 Err 为 0，同时用 ad1/ad2 差值约束电感平衡。
 * 原因是落地后单看 Err 可能受瞬态计算影响，双条件可以减少偏线误退出。
 *
 * @return int8 1-已接近中线，0-仍需继续低速回正。
 */
static int8 fly_is_center_line(void)
{
    if (func_abs((int)ad1 - (int)ad2) < 10 &&
        Err > -1.0f && Err < 1.0f)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 复位飞坡状态机内部计数并回到普通巡线。
 *
 * 关闭飞坡开关或现场调试强制退出时，需要同时清掉阶段计数，避免重新开启后
 * 沿用上一次弱磁窗口中的残留计数而误入飞坡。
 */
static void fly_reset_state(void)
{
    fly_detect_count = 0;
    fly_state_count = 0;
    flat_fly = FLY_STATE_IDLE;
}

/**
 * @brief 启动状态机更新
 * @details 10ms 调用一次，检测 P36 启动按键或外部命令，在停止、预启动、运行之间切换
 */
void a_run_mode_update_start_state(void)
{
    /* 外部命令可直接请求进入运行态，flat_statr == 3 为一次性触发 */
    if (flat_statr == 3)
    {
        current_start_state = START_STATE_2;
        flat_statr = 2; // 同步状态镜像为运行中
        return;
    }
    else if (flat_statr == 0 && current_start_state != START_STATE_0)
    {
        // 外部命令要求停止时，直接回到停止态
        current_start_state = START_STATE_0;
        return;
    }

    /* 检测按键按下（低电平有效） */
    if (P36 == 0)
    {
        if (key_released == 1) // 仅在本次按下的首次稳定阶段计数
        {
            /* 仅在按键保持按下期间递增，形成时间窗消抖 */
            press_debounce_cnt++;
            if (press_debounce_cnt >= START_DEBOUNCE_TIME)
            {
                /* 消抖通过后切换到下一个有效状态 */
                if (current_start_state == START_STATE_0 || current_start_state == START_STATE_2)
                {
                    current_start_state = START_STATE_1;
                }
                else if (current_start_state == START_STATE_1)
                {
                    current_start_state = START_STATE_2;
                }

                /* 原因：不重复触发按下的去抖。防止用户在物理抖动瞬间或持续按下时系统在连续多个状态间极速切换。 */
                key_released = 0;
                press_debounce_cnt = 0;
            }
        }
    }
    /* 按键释放（高电平） */
    else
    {
        press_debounce_cnt = 0;
        key_released = 1; // 解除锁存，等待下一次按下
    }
}

/**
 * @brief 读取当前启动状态
 * @return int8 当前状态值：0-停止，1-预启动，2-运行中
 */
int8 a_run_mode_get_start_state(void)
{
    return (int8)current_start_state;
}

/**
 * @brief 负压状态更新
 * @details 启动状态有效且配置允许时才更新负压，避免待机时误动作
 */
void a_run_mode_update_fuya_state(void)
{
    if (a_run_mode_get_start_state() >= 1 && app.start.start_flag == 1)
    {
        fuya_update_simple();
    }
    else
    {
        fuya_force_stop();
    }
}

/**
 * @brief 飞坡速度修正
 * @details 根据四路电感特征推进飞坡状态机，并在高风险阶段覆盖速度和转向输出。
 * @param speed 输出的目标速度指针
 */
void a_run_mode_update_fly_speed(int *speed)
{
    if (app.fly.fly_ramp_enable != 1)
    {
        fly_reset_state();
        return;
    }

    switch (flat_fly)
    {
    case FLY_STATE_IDLE:
        if (fly_is_ramp_lost_signal())
        {
            fly_detect_count++;
            if (fly_detect_count >= app.fly.count_fly_time_1)
            {
                fly_detect_count = 0;
                fly_state_count = 0;
                flat_fly = FLY_STATE_HOLD;
            }
        }
        else
        {
            fly_detect_count = 0;
        }

        if (flat_fly != FLY_STATE_HOLD)
        {
            break;
        }
        /* 触发成立的同一控制周期立即锁角，避免飞坡入口多放行一个 5ms 周期。 */

    case FLY_STATE_HOLD:
        /* 离地/弱磁期间冻结外环目标，避免 Err 瞬态失真把车头拉偏。 */
        *speed = app.fly.count_fly_speed;
        PID.steer.output = (float)FLY_HOLD_ANGLE;

        fly_state_count++;
        if (fly_state_count >= app.fly.count_fly_time_2)
        {
            fly_state_count = 0;
            flat_fly = FLY_STATE_RECOVER;
        }
        break;

    case FLY_STATE_RECOVER:
        /*
         * 下地后第一件事是用飞坡低速回到中线。这里不再锁角，
         * 让电感外环按 Err 回正，直到有效线信号下 Err 接近 0。
         */
        *speed = app.fly.count_fly_speed;
        if (fly_is_center_line())
        {
            fly_state_count++;
            if (fly_state_count >= FLY_RECOVER_COUNT)
            {
                fly_state_count = 0;
                flat_fly = FLY_STATE_COOLDOWN;
            }
        }
        else
        {
            fly_state_count = 0;
        }
        break;

    case FLY_STATE_COOLDOWN:
        /* 冷却期只禁止重复触发，不覆盖控制输出，给普通巡线一个稳定接管窗口。 */
        fly_state_count++;
        if (fly_state_count >= FLY_COOLDOWN_COUNT)
        {
            fly_reset_state();
        }
        break;

    default:
        fly_reset_state();
        break;
    }
}

void run_mode_update_angle_target(float *angle_target)
{
    /* 环岛阶段固定目标角速度，最终差速仍交给角速度内环闭环输出。 */
    if (ring_data.diff_set != 0)
    {
        *angle_target = ring_data.diff_set;
    }
}

/* ---------------- 环岛状态机 ---------------- */

/**
 * @brief 环岛阶段枚举
 * @details 描述右环识别、入环、环内和出环的各个状态
 */
enum RingStep
{
    no_ring,      // 未进入环岛流程
    ring,         // 已识别到右环入口
    pre_ring,     // 预入环阶段
    in_ring,      // 环内阶段
    pre_out_ring, // 预出环阶段
    out_ring      // 出环确认阶段
};

enum TrackElement
{
    ELEMENT_NONE = 0,
    ELEMENT_LEFT_RING = 1,
    ELEMENT_RIGHT_RING = 2,
    ELEMENT_CYLINDER = 3
};

enum CylinderStep
{
    CYL_IDLE = 0,
    CYL_WAIT_TOP = 1,
    CYL_WAIT_GROUND = 2,
    CYL_STABLE_DELAY = 3
};

// 当前环岛状态机状态
enum RingStep current_state = no_ring;

// 环岛过程数据，保存累计量和阶段标志
RingStruct ring_data = {0};
static int8 ring_pose_flat = 1; /* 姿态门控结果：1-允许圆环识别，0-桶/墙/坡面段禁止圆环 */
static uint8 ring_entry_count = 0;
static uint8 ring_finish_event = 0;
static enum TrackElement expected_element = ELEMENT_LEFT_RING;
static enum CylinderStep cylinder_state = CYL_IDLE;
static uint8 cylinder_top_count = 0;
static uint8 cylinder_ground_count = 0;
static uint8 cylinder_stable_count = 0;

/**
 * @brief 读取当前环岛状态机阶段。
 * @return int8 当前阶段编号：0-no_ring，1-ring，2-pre_ring，3-in_ring，4-pre_out_ring，5-out_ring。
 *
 * @note 仅供菜单和调试显示读取，不应由外部模块直接驱动状态迁移。
 */
int8 a_run_mode_get_ring_state(void)
{
    return (int8)current_state;
}

/**
 * @brief 读取当前圆环姿态门控结果。
 * @return int8 1-姿态接近平地，允许圆环识别；0-疑似桶/墙/坡面姿态，禁止圆环识别。
 */
int8 a_run_mode_get_ring_pose_flat(void)
{
    return ring_pose_flat;
}

int8 a_run_mode_get_expected_element(void)
{
    return (int8)expected_element;
}

int8 a_run_mode_get_cylinder_state(void)
{
    return (int8)cylinder_state;
}

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
    ring_data.star_l = 0;
    ring_data.star_r = 0;
    ring_data.condition = 0;
    ring_data.last_yaw = 0;
    ring_data.diff_set = 0;
    ring_data.distance = 0;
    ring_data.encoder = 0;
    ring_data.gyro_flat = 0;
    ring_data.Gyroz = 0;
    ring_entry_count = 0;
    ring_finish_event = 0;
    current_state = no_ring;
}

/**
 * @brief 判断当前姿态是否允许圆环入口识别。
 *
 * 立体桶、墙面和坡面会改变车身重力方向，电感形态可能短暂接近圆环入口。
 * 这里仅使用重力向量 Z 分量做滞回判断，避免 pitch 方向定义变化影响圆环门控。
 *
 * @return int8 1-接近平地，允许圆环识别；0-非平地姿态，禁止圆环识别。
 */
static int8 ring_is_flat_pose(void)
{
    float vzc;

    vzc = fuya_last_vzc;

    if (ring_pose_flat != 0)
    {
        if (vzc < RING_FLAT_BLOCK_VZ)
        {
            ring_pose_flat = 0;
        }
    }
    else
    {
        if (vzc >= RING_FLAT_RELEASE_VZ)
        {
            ring_pose_flat = 1;
        }
    }

    return ring_pose_flat;
}

/**
 * @brief 判断左环入口电感特征是否命中。
 * @return int8 1-命中左环入口特征，0-未命中。
 *
 */
static int8 ring_is_left_entry_signal(void)
{
    if ((ad1 > 35 &&
         ad2 > 15 &&
         ad3 > 15 &&
         ad4 > 35 &&
         ad1 < 90 &&
         ad2 < 40 &&
         ad3 < 40 &&
         ad4 < 90) ||
        (ad1 > 45 &&
         ad2 > 15 &&
         ad3 > 5 &&
         ad4 > 30 &&
         ad1 < 90 &&
         ad2 < 40 &&
         ad3 < 40 &&
         ad4 < 90))
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 读取5ms主环缓存的重力向量Z分量。
 * @return float 已在主控制入口限幅到 -1.0~1.0 的姿态Z分量。
 *
 * @note 圆筒状态机跟随 run_time_1() 调用，直接复用同周期姿态缓存，避免多处重复计算四元数。
 */
static float cylinder_read_vzc(void)
{
    return fuya_last_vzc;
}

static void cylinder_reset_state(void)
{
    cylinder_top_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_IDLE;
    fuya_restore_cylinder_peak_angle();
}

static void cylinder_start_wait_top(void)
{
    cylinder_top_count = 0;
    cylinder_ground_count = 0;
    cylinder_stable_count = 0;
    cylinder_state = CYL_WAIT_TOP;
    fuya_restore_cylinder_peak_angle();
}

static uint8 cylinder_update_10ms(void)
{
    float vzc;

    if (cylinder_state == CYL_IDLE)
    {
        cylinder_start_wait_top();
    }

    vzc = cylinder_read_vzc();

    switch (cylinder_state)
    {
    case CYL_WAIT_TOP:
        if (vzc <= CYLINDER_TOP_VZ)
        {
            cylinder_top_count++;
            if (cylinder_top_count >= CYLINDER_TOP_CONFIRM_COUNT)
            {
                cylinder_top_count = 0;
                cylinder_ground_count = 0;
                cylinder_stable_count = 0;
                fuya_apply_cylinder_peak_angle();
                cylinder_state = CYL_WAIT_GROUND;
                //  stop = 1;
            }
        }
        else
        {
            cylinder_top_count = 0;
        }
        break;

    case CYL_WAIT_GROUND:
        if (vzc >= CYLINDER_GROUND_VZ)
        {
            cylinder_ground_count++;
            if (cylinder_ground_count >= CYLINDER_GROUND_CONFIRM_COUNT)
            {
                cylinder_ground_count = 0;
                cylinder_stable_count = 0;
                cylinder_state = CYL_STABLE_DELAY;
                //                stop = 1;
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
            //            stop = 1;
            return 1;
        }
        break;

    default:
        cylinder_reset_state();
        break;
    }

    return 0;
}

static void track_element_reset_state(void)
{
    expected_element = ELEMENT_LEFT_RING;
    ring_reset_state();
    cylinder_reset_state();
}

void a_run_mode_update_track_element_gate(void)
{
    uint8 cylinder_done;

    if (app.start.circle_flags != 1)
    {
        track_element_reset_state();
        return;
    }

    /* 未实现的右环相关模式在运行时按模式 0 执行，避免现场误选后关闭特殊元素。 */

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
        cylinder_done = cylinder_update_10ms();
        if (cylinder_done != 0)
        {
            expected_element = ELEMENT_LEFT_RING;
        }
        return;
    }

    expected_element = ELEMENT_LEFT_RING;
}

/**
 * @brief 左环状态机更新
 * @details 根据电感特征、编码器累计和角速度累计结果推进左环流程。
 * 注意此部分为高层状态机，不涉及高频浮点解算，但条件判断需防抖。
 */
void circle_check_l(uint8 allow_entry)
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
            ring_is_flat_pose() != 0 &&
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
        ring_data.Gyroz = 0;

        /* 累计距离达到阈值后进入预入环阶段 */
        if (ring_data.encoder >= app.ring.ring_entry_encoder)
        {
            ring_data.distance = 0;
            ring_data.encoder = 0;
            ring_data.last_yaw = imu660rc_yaw; // 记录预入环阶段初始航向角
            ring_data.gyro_flat = 1;
            ring_data.Gyroz = 0;
            current_state = pre_ring; // 切换到预入环阶段
        }
        break;

    case pre_ring:
        /* 给定预入环固定目标角速度，左环实测 Gyroz 为负。 */
        ring_data.diff_set = app.ring.pre_ring_Gyro_target;

        /* 相对预入环起点的偏航角累计达到入环阈值并确认 50ms 后，认为已真正入环。 */
        /* 由于累加的角度自带符号，这里直接取绝对值判断是否转够30度即可，不论左右环。 */
        if (ring_data.Gyroz < -app.ring.pre_ring_Gyroz && timeadd(&ring_data.ing_ring_time, 50))
        {
            ring_data.diff_set = 0;
            timedestroy(&ring_data.ing_ring_time);
            current_state = in_ring;
        }
        break;

    case in_ring:
        /* 左环 Gyroz 为负，必须达到负向环内阈值后才进入预出环。 */
        if (ring_data.Gyroz <= -app.ring.in_ring_Gyroz)
        {
            current_state = pre_out_ring;
        }
        break;

    case pre_out_ring:
        /* 给定预出环固定目标角速度，继续沿左环方向修正车身。 */
        ring_data.diff_set = app.ring.pre_out_ring_Gyro_target;

        /* 预出环继续沿左环方向打到更大的出环角度，避免过早回线导致压线不稳。 */
        if (ring_data.Gyroz < -app.ring.pre_out_ring_Gyroz && timeadd(&ring_data.out_ring_time, 50))
        {
            ring_data.diff_set = 0;
            timedestroy(&ring_data.out_ring_time);
            ring_data.gyro_flat = 0;
            ring_data.Gyroz = 0;
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
            ring_data.Gyroz = 0;                   // 清零相对偏航角差
            current_state = no_ring;               // 返回普通巡线状态
            ring_finish_event = 1;
            //           stop = 1;
        }
        break;
    }
}

/**
 * @brief 更新环岛判定所需的里程与偏航量
 * @details
 * `encoder` 继续按速度估计累计里程；`Gyroz` 现在使用前后两次yaw的差值进行增量累加。
 * 这种方式可以避免跨0点跳变，并且累加出来的是实际转过的总角度量，符合转角触发阈值。
 *
 * @note 该函数依赖进入 `pre_ring` 时已经正确记录 `last_yaw`。
 */
void gyro_integrals(void)
{
    /* 环岛阶段只关心相对转过多少角，通过前后两次yaw的差值进行增量累加。 */
    if (ring_data.gyro_flat == 1)
    {
        /* 计算当前yaw与上一次yaw的差值。 */
        float delta_yaw = imu660rc_yaw - ring_data.last_yaw;

        /* 处理跨越0点（或360点）的情况，将角差折返到 -180~180。 */
        if (delta_yaw > 180.0f)
        {
            delta_yaw -= 360.0f;
        }
        else if (delta_yaw < -180.0f)
        {
            delta_yaw += 360.0f;
        }

        /* 增量累加到总角度。这里取绝对值，因为我们只关心“转了多少度”。 */
        /* 如果要分左右环区分正负，则根据打角方向进行符号处理，或者就直接累加。*/
        /* 考虑到之前逻辑左环(pre_ring_Gyro_set>0)转角为正，右环(pre_ring_Gyro_set<0)转角为负：*/
        ring_data.Gyroz += delta_yaw;

        /* 更新 last_yaw 供下一次计算使用 */
        ring_data.last_yaw = imu660rc_yaw;
    }

    /* 编码器累计使能时，累加当前前进距离估计 */
    if (ring_data.distance == 1)
    {
        /* 0.01 系数与当前速度单位配套，保持里程判据量级稳定 */
        ring_data.encoder += (speed_l + speed_r) * 0.005; // 用于判断是否达到环岛距离阈值
    }
}
