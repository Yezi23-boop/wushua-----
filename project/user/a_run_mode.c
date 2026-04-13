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
static int flat_statr_date = 0; /* 启动状态缓存，当前保留未使用 */
static int count_fly_1 = 0;     /* 飞坡进入判定计数 */
static int count_fly_2 = 0;     /* 飞坡保持阶段计数 */

/* --- 启停状态机参数 --- */
#define START_DEBOUNCE_TIME 20 /* 启动按键消抖确认次数（10ms 调用周期下约 500ms） */

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
 * @brief 启动状态机更新
 * @details 10ms 调用一次，检测 P36 启动按键或外部命令，在停止、预启动、运行之间切换
 */
void a_run_mode_update_start_state(void)
{
    /* 记录外部镜像状态，供后续扩展状态迁移诊断使用 */
    flat_statr_date = flat_statr;

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

                /* 锁存本次按下，避免长按期间重复切换 */
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
 * @details 根据四路电感特征判断是否进入飞坡阶段，并在飞坡期间覆盖速度和转向输出
 * @param speed 输出的目标速度指针
 */
void a_run_mode_update_fly_speed(int *speed)
{
    /* A. 满足电感特征时触发飞坡检测，避免重复进入 */
    if (app.fly.fly_ramp_enable == 1 && ad1 < 40 && ad2 < 15 && ad3 < 15 && ad4 < 40 && flat_fly == 0)
    {
        count_fly_1++;
        if (count_fly_1 >= app.fly.count_fly_time_1)
        {
            count_fly_1 = 0;
            flat_fly = 1; /* 进入飞坡阶段 */
        }
    }

    /* B. 飞坡阶段覆盖速度与角度输出 */
    if (flat_fly == 1)
    {
        /* 使用配置中的飞坡速度和锁舵角 */
        *speed = app.fly.count_fly_speed;
        PID.steer.output = (float)app.fly.count_fly_angle;

        count_fly_2++;
        /* 飞坡保持计时结束后自动退出 */
        if (count_fly_2 >= app.fly.count_fly_time_2)
        {
            count_fly_2 = 0;
            flat_fly = 0;
        }
    }

    /* 非飞坡阶段按赛道策略回退到基础巡线速度 */
    if (ring_data.flast_r == 1)
    {
        /* C. 环岛阶段当前仍沿用基础速度 */
        *speed = (int)app.speed.speed_run;
    }
    else
    {
        /* C. 普通巡线阶段沿用基础速度 */
        *speed = (int)app.speed.speed_run;
    }
}

void run_mode_update_angle_output(float *angle)
{
    /* 环岛阶段可用 Gyroz_set 强制覆盖角度环输出，实现固定打角策略 */
    if (ring_data.Gyroz_set != 0)
    {
        *angle = ring_data.Gyroz_set;
    }
    else
    {
        *angle = PID.angle.output;
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

// 当前环岛状态机状态
enum RingStep current_state = no_ring;

// 环岛过程数据，保存累计量和阶段标志
RingStruct ring_data = {0};

/**
 * @brief 右环状态机更新
 * @details 根据电感特征、编码器累计和角速度累计结果推进右环流程
 */
void circle_check_r()
{
    /* 右环入口特征连续命中计数 */
    static uint8 count_start = 0;

    /* 状态机每次只推进一步，确保计时与传感器判定可追踪 */
    switch (current_state)
    {
    case no_ring:
        /* 1. 根据电感特征识别右环入口 */
        if (ad1 > 30 && ad2 < 50 && ad3 > 80 && ad4 > 90)
        {
            count_start++;
        }

        /* 2. 在限定时间内连续命中多次才确认进入环岛 */
        if (count_start > 0)
        {
            /* 在 1000ms 窗口内达到 3 次，判定右环成立 */
            if (count_start >= 3)
            {
                count_start = 0;
                timedestroy(&ring_data.time_r); // 清空右环识别定时器
                ring_data.flast_r = 1;          // 置位右环过程标志
                /* 从入口识别切到 ring，后续进入距离累计阶段 */
                current_state = ring; // 切换到环岛准备阶段
            }
            /* 超过 1000ms 仍未满足次数，丢弃本次识别 */
            else if (timeadd(&ring_data.time_r, 1000))
            {
                count_start = 0;
            }
        }
        break;

    case ring:
        /* 清除环岛角速度给定，开始累计编码器距离 */
        ring_data.Gyroz_set = 0;
        ring_data.distance = 1; // 允许累计编码器里程

        /* 累计距离达到阈值后进入预入环阶段 */
        if (ring_data.encoder >= app.ring.ring_encoder)
        {
            ring_data.distance = 0;
            ring_data.encoder = 0;
            current_state = pre_ring; // 切换到预入环阶段
        }
        break;

    case pre_ring:
        /* 给定预入环打角目标 */
        ring_data.Gyroz_set = app.ring.pre_ring_Gyro_set;

        /* 角速度累计达到阈值并持续 50ms 后，认为已真正入环 */
        if (ring_data.Gyroz > 30 && timeadd(&ring_data.ing_ring_time, 50))
        {
            ring_data.Gyroz_set = 0;               // 关闭预入环角速度给定
            timedestroy(&ring_data.ing_ring_time); // 清空入环确认计时器
            current_state = in_ring;               // 切换到环内阶段
        }
        break;

    case in_ring:
        /* 环内角速度达到出环阈值后，进入预出环阶段 */
        if (ring_data.Gyroz >= app.ring.pre_out_ring_Gyroz)
        {
            /* 进入预出环后由目标角速度引导车辆平顺回线 */
            current_state = pre_out_ring; // 切换到预出环阶段
        }
        break;

    case pre_out_ring:
        /* 给定预出环打角目标 */
        ring_data.Gyroz_set = app.ring.pre_out_ring_Gyro_set;

        /* 角速度回落并持续 50ms 后，进入正式出环阶段 */
        if (ring_data.Gyroz < app.ring.pre_out_ring_Gyroz && timeadd(&ring_data.out_ring_time, 50))
        {
            ring_data.Gyroz_set = 0;               // 关闭预出环角速度给定
            timedestroy(&ring_data.out_ring_time); // 清空出环确认计时器
            ring_data.gyro_flat = 0;               // 关闭角速度累计
            current_state = out_ring;              // 切换到出环确认阶段
        }

        break;

    case out_ring:
        /* 1000ms 内电感重新平衡，则认为已完全驶离环岛 */
        if (func_abs((int)(ad1 - ad4)) < 10 && timeadd(&ring_data.out_ring_time, 1000))
        {
            timedestroy(&ring_data.out_ring_time); // 清空出环确认定时器
            ring_data.flast_r = 0;                 // 清除右环过程标志
            ring_data.Gyroz_set = 0;               // 清除环岛角速度给定
            current_state = no_ring;               // 返回普通巡线状态
        }
        break;
    }
}

/**
 * @brief 更新环岛判定所需的累计量
 * @details 根据使能标志累计 Z 轴角速度和编码器里程，用于环岛阶段切换判定
 * @note 这些累计量需要在环岛开始和结束阶段及时清零
 */
void gyro_integrals()
{
    /* 角速度累计使能时，累加校准后的 gyro_z */
    if (ring_data.gyro_flat == 1)
    {
        /* Gyroz 为离散累加量，阈值需与采样周期共同校准 */
        ring_data.Gyroz += gyro_z; // 用于判断是否完成入环/出环转向
    }

    /* 编码器累计使能时，累加当前前进距离估计 */
    if (ring_data.distance == 1)
    {
        /* 0.01 系数与当前速度单位配套，保持里程判据量级稳定 */
        ring_data.encoder += (speed_l + speed_r) * 0.01; // 用于判断是否达到环岛距离阈值
    }
}
