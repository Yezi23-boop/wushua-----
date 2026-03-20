#include "zf_common_headfile.h"
#include "a_run_mode.h"

/* --- 内部私有变量 --- */
static int flat_statr_date = 0; /* 启动按键长按防抖与计时器 */
static int count_fly_1 = 0;     /* 飞坡入场触发特征的连续检测计数 */
static int count_fly_2 = 0;     /* 飞坡执行过程中的持续时间计数 */

/* --- 启动状态机相关常量与变量 --- */
#define START_DEBOUNCE_TIME 50 // 按键防抖时间阈值（调度周期数）

enum StartState
{
    START_STATE_0 = 0, // 初始未启动状态
    START_STATE_1 = 1, // 第1次按键后的状态
    START_STATE_2 = 2  // 第2次按键后的状态
};

static enum StartState current_start_state = START_STATE_0; // 启动状态机当前状态
static int press_debounce_cnt = 0;                          // 按键防抖计时
static int8 key_released = 1;                               // 按键释放标志：1-已释放，0-未释放

/**
 * @brief 启动状态机（10ms定时运行，只更新内部状态）
 * @details 检测P36引脚状态，每次按键切换状态：初始0 -> 第1次按1 -> 第2次按2 -> 第3次按1...
 *         采用严格的按下防抖和松开检测，避免长按导致多次触发。
 */
void a_run_mode_update_start_state(void)
{
    /* 允许 VOFA 强制覆盖状态，如果是 VOFA 写入了启动指令(比如 flat_statr被设为3) */
    if (flat_statr == 3)
    {
        current_start_state = START_STATE_2;
        flat_statr = 2; // 归位，避免重复触发
        return;
    }
    else if (flat_statr == 0 && current_start_state != START_STATE_0)
    {
        // 允许 VOFA 发送 STOP 命令强制归零
        current_start_state = START_STATE_0;
        return;
    }

    /* 按键被按下 (低电平) */
    if (P36 == 0)
    {
        if (key_released == 1) // 确保上一次按键已经松开
        {
            press_debounce_cnt++;
            if (press_debounce_cnt >= START_DEBOUNCE_TIME)
            {
                /* 防抖完成，确认是一次有效按键 */
                if (current_start_state == START_STATE_0 || current_start_state == START_STATE_2)
                {
                    current_start_state = START_STATE_1;
                }
                else if (current_start_state == START_STATE_1)
                {
                    current_start_state = START_STATE_2;
                }

                /* 标记按键已被处理，等待下一次松开 */
                key_released = 0;
                press_debounce_cnt = 0;
            }
        }
    }
    /* 按键已松开 (高电平) */
    else
    {
        press_debounce_cnt = 0;
        key_released = 1; // 恢复松开标志
    }
}

/**
 * @brief 读取当前启动状态
 * @return int8 当前状态值：0-未启动，1-状态1，2-状态2
 */
int8 a_run_mode_get_start_state(void)
{
    return (int8)current_start_state;
}

/**
 * @brief 更新负压系统工作状态
 * @details 只有在小车处于运行准备或正式运行状态（flat_statr >= 1）
 * 并且全局配置中允许开启（start_flag == 1）时，才调用底层负压更新逻辑。
 */
void a_run_mode_update_fuya_state(void)
{
    if (a_run_mode_get_start_state() >= 1 && app.start.start_flag == 1)
    {
        fuya_update_simple();
    }
}

/**
 * @brief 飞坡与特殊慢速区域处理策略
 * @details
 * 1. 监测四路电感，当出现特定微弱或全丢特征时，判定前方可能为飞坡。
 * 2. 触发后，将当前期望速度强制锁定为预设的慢速值，同时输出固定的舵机转向角。
 * 3. 维持特定的计时周期后，自动退出飞坡模式，恢复常规循迹控制。
 * @param speed 指向当前目标速度的指针，根据状态直接在此函数内修改。
 */
void a_run_mode_update_fly_speed(int *speed)
{
    /* A. 飞坡进入判定：电感值均小于特定阈值，且飞坡模式开关已在配置中开启 */
    if (app.fly.fly_ramp_enable == 1 && ad1 < 40 && ad2 < 15 && ad3 < 15 && ad4 < 40 && flat_fly == 0)
    {
        count_fly_1++;
        if (count_fly_1 >= app.fly.count_fly_time_1)
        {
            count_fly_1 = 0;
            flat_fly = 1; /* 触发并进入飞坡模式 */
        }
    }

    /* B. 飞坡模式下的强制接管执行 */
    if (flat_fly == 1)
    {
        /* 强制使用 EEPROM 配置中的飞坡慢速和预设转向打角 */
        *speed = app.fly.count_fly_speed;
        PID.steer.output = (float)app.fly.count_fly_angle;

        count_fly_2++;
        /* 飞坡维持计时结束，退出飞坡模式 */
        if (count_fly_2 >= app.fly.count_fly_time_2)
        {
            count_fly_2 = 0;
            flat_fly = 0;
        }
    }
    if (ring_data.flast_r == 1)
    {
        /* C. 常规状态下，采用配置的基础巡航速度 */
        *speed = (int)app.speed.speed_run;
    }
    else
    {
        /* C. 常规状态下，采用配置的基础巡航速度 */
        *speed = (int)app.speed.speed_run;
    }
}

void run_mode_update_angle_output(float *angle)
{
    if (ring_data.Gyroz_set != 0)
    {
        *angle = ring_data.Gyroz_set;
    }
    else
    {
        *angle = PID.angle.output;
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief 环岛状态枚举
 * @details 定义了环岛识别与处理过程中的各个独立阶段
 */
enum RingStep
{
    no_ring,      // 无环岛状态（常规巡线）
    ring,         // 环岛特征识别确认状态
    pre_ring,     // 转进环岛状态（入环打角动作）
    in_ring,      // 环岛中巡航状态
    pre_out_ring, // 准备转出环岛状态（出环打角动作）
    out_ring      // 环岛出口状态（出环后恢复阶段）
};

// 当前环岛状态机所处的阶段
enum RingStep current_state = no_ring;

// 环岛相关数据结构体实例，存储了各类状态、定时器及积分信息
RingStruct ring_data = {0};

/**
 * @brief 右环岛检测与状态机处理主函数
 * @details 使用分段状态机处理右环岛的检测、入环、环内巡航和出环的完整闭环。
 */
void circle_check_r()
{
    /* 用于检测环岛特征点出现次数的静态变量 */
    static uint8 count_start = 0;

    switch (current_state)
    {
    case no_ring:
        /* 1. 任何时候只要特征电感条件满足，就累加特征出现次数 */
        if (ad1 > 30 && ad2 < 50 && ad3 > 80 && ad4 > 90)
        {
            count_start++;
        }

        /* 2. 只要捕捉到至少一次特征，就启动 1000ms 的时间观测窗口 */
        if (count_start > 0)
        {
            /* 如果在 1000ms 的观测窗口内，特征出现次数达到 3 次，则确认入环 */
            if (count_start >= 3)
            {
                count_start = 0;
                timedestroy(&ring_data.time_r); // 确认成功，销毁观测定时器
                ring_data.flast_r = 1;          // 标记右环岛识别成功
                current_state = ring;           // 切换至环岛确认状态
            }
            /* 如果 1000ms 时间耗尽但次数未达标，说明是假特征干扰，清零重新来 */
            else if (timeadd(&ring_data.time_r, 1000))
            {
                count_start = 0;
            }
        }
        break;

    case ring:
        /* 准备入环前的数据重置与初始化 */
        ring_data.Gyroz_set = 0;
        ring_data.distance = 1; // 开启编码器积分计算行驶距离

        /* 当编码器积分累积距离达到设定的入环阈值时，开始执行入环打角 */
        if (ring_data.encoder >= app.ring.ring_encoder)
        {
            ring_data.distance = 0;
            ring_data.encoder = 0;
            current_state = pre_ring; // 切换到转进环岛状态
        }
        break;

    case pre_ring:
        /* 设置期望的入环转向打角（Gyroz_set） */
        ring_data.Gyroz_set = app.ring.pre_ring_Gyro_set;

        /* 时间窗口逻辑：50ms 内观测，如果陀螺仪积分角度突破 30 度，说明车头已经成功转入环岛 */
        if (ring_data.Gyroz > 30 && timeadd(&ring_data.ing_ring_time, 50))
        {
            ring_data.Gyroz_set = 0;               // 切换到环内巡航角速度设定
            timedestroy(&ring_data.ing_ring_time); // 动作达标，销毁当前阶段定时器
            current_state = in_ring;               // 切换到环岛中巡航状态
        }
        break;

    case in_ring:
        /* 环内巡航阶段：一直累加陀螺仪积分，直到完成一整圈（达到出环期望角度） */
        if (ring_data.Gyroz >= app.ring.pre_out_ring_Gyroz)
        {
            current_state = pre_out_ring; // 切换到准备出环状态
        }
        break;

    case pre_out_ring:
        /* 设置期望的出环转向打角 */
        ring_data.Gyroz_set = app.ring.pre_out_ring_Gyro_set;

        /* 时间窗口逻辑：50ms 内观测，如果陀螺仪积分角度回落到出环阈值以内，说明车头已转正准备出环 */
        if (ring_data.Gyroz < app.ring.pre_out_ring_Gyroz && timeadd(&ring_data.out_ring_time, 50))
        {
            ring_data.Gyroz_set = 0;               // 切换到环岛出口角速度设定
            timedestroy(&ring_data.out_ring_time); // 动作达标，销毁定时器
            ring_data.gyro_flat = 0;               // 关闭陀螺仪角度积分
            current_state = out_ring;              // 切换到环岛出口状态
        }

        break;

    case out_ring:
        /* 出口状态观测：1000ms 时间窗口内，如果外侧电感差比和恢复平稳（<10），说明已彻底驶出环岛 */
        if (func_abs((int)(ad1 - ad4)) < 10 && timeadd(&ring_data.out_ring_time, 1000))
        {
            timedestroy(&ring_data.out_ring_time); // 确认出环，销毁定时器
            ring_data.flast_r = 0;                 // 标记右环岛识别成功
            ring_data.Gyroz_set = 0;               // 切换到环岛出口角速度设定
            current_state = no_ring;               // 状态机闭环，切换回无环岛寻线状态
        }
        break;
    }
}

/**
 * @brief 陀螺仪与编码器后台积分计算函数
 * @details 挂载在主控制周期内，根据环岛状态机的标志位，
 * 计算陀螺仪 Z 轴角速度的积分值（得到角度）和编码器的累计值（得到距离）。
 * @note 这些积分值是环岛各阶段状态切换的核心判断依据。
 */
void gyro_integrals()
{
    /* 如果环岛状态机开启了陀螺仪积分标志 */
    if (ring_data.gyro_flat == 1)
    {
        ring_data.Gyroz += gyro_z; // 将校准后的角速度按周期累加，积分成角度
    }

    /* 如果环岛状态机开启了距离计算标志 */
    if (ring_data.distance == 1)
    {
        ring_data.encoder += (speed_l + speed_r) * 0.01; // 累加左右轮速度换算为行驶距离
    }
}
