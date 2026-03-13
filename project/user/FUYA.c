#include "zf_common_headfile.h"
#include <math.h>

// 全局变量：负压状态与历史数据
int fuya_date = 0.0f;		   // 当前输出给负压执行器的平滑占空比
float fuya_date_factor = 0.0f; // 未滤波的目标占空比，用于调试观察
uint8 phase = 0;			   // 当前阶段编号（0~4）

// 负压基准参数（单位：占空比，0~10000）——根据结构四个切点标定
// #define DUTY_BOTTOM 500.0f		 // 下切点（平地）基准
float DUTY_BOTTOM = 0;			 // 下切点（平地）基准
#define DUTY_LEFT 4500.0f		 // 左切点（左侧竖墙）基础值
#define DUTY_RIGHT 4000.0f		 // 右切点（右侧竖墙）基础值
#define DUTY_TOP 4000.0f		 // 上切点（倒立）基准
#define PREFERENCE_OFFSET 500.0f // 前向优待最大偏移量（±250）

// 姿态阈值（基于 IMU 单位向量 vzc/vxc 划分场景）
#define VZC_GROUND_THRESH 0.93f
#define VZC_VERTICAL_THRESH 0.15f
#define VZC_INVERT_THRESH -0.9f
#define VXC_PLANE_MARGIN 0.06f
#define VXC_WALL_MARGIN 0.06f
#define VXC_WALL_RELEASE 0.02f

// 平滑系数 alpha：不同阶段的响应速度需求不同，单独配置
#define ALPHA_UP1 0.92f	   // 阶段0/1：平地及平地上墙，需迅速贴合目标
#define ALPHA_UP2 0.65f	   // 阶段2：左墙向天花板，提高倒立响应
#define ALPHA_DOWN1 0.55f  // 阶段3：天花板向右墙，加快释放速度
#define ALPHA_DOWN2 0.45f  // 阶段4：右墙向平地，仍保持适度柔和但更快
#define ALPHA_DEFAULT 0.8f // 过渡阶段默认响应

//// 负压输出限幅（硬件/安全双重限制）
//#define DUTY_MIN 1800.0f // 最低保障占空比，防止吸力过小
//#define DUTY_MAX 4000.0f // 允许的最大占空比（含前向优待）

/**
 * @brief 负压吸附系统初始化函数
 */
void fuya_Init(void)
{
	pwm_init(PWMA_CH2N_P03, 17000, 0); // 初始化风扇PWM（40kHz）
}

/**
 * @brief 输出占空比到风扇（负压吸附执行器）
 * @param pwm 目标占空比（需为非负值，因为风扇/泵通常只正向工作）
 * @note 控制风扇转速，占空比越大，吸力越强
 */
void fuya_motor_output(int pwm)
{
	// 简单限幅，避免越界
	if (pwm < 0)
		pwm = 0;
	if (pwm > 4000)
		pwm = 4000;
	pwm_set_duty(PWMA_CH2N_P03, (uint32)pwm);
}

/**
 * @brief 判断当前所处阶段（基于 vzc/vxc 组合）
 * @return 阶段编号：0=平地，1=平地上墙，2=墙上天花板，3=天花板下墙，4=墙下平地
 * @note 阈值取自 VZC/VXC，保证五段互斥并覆盖整圈运动
 */
uint8 get_current_phase(float vzc, float vxc)
{
	if (vzc >= VZC_GROUND_THRESH && func_abs(vxc) <= VXC_PLANE_MARGIN)
	{
		return 0; // 平地
	}
	else if (vzc >= VZC_VERTICAL_THRESH && vzc < VZC_GROUND_THRESH && vxc <= -VXC_WALL_MARGIN)
	{
		return 1; // 平地上墙：vzc 1→0，vxc 0→-1
	}
	else if (vzc >= -1.0f && vzc <= VZC_VERTICAL_THRESH && vxc <= VXC_WALL_RELEASE)
	{
		return 2; // 墙上天花板：vzc 0→-1，vxc -1→0
	}
	else if (vzc >= -1.0f && vzc <= VZC_VERTICAL_THRESH && vxc > VXC_WALL_RELEASE)
	{
		return 3; // 天花板下墙：vzc -1→0，vxc 0→1
	}
	else if (vzc >= VZC_VERTICAL_THRESH && vzc < VZC_GROUND_THRESH && vxc > VXC_WALL_MARGIN)
	{
		return 4; // 墙下平地：vzc 0→1，vxc 1→0
	}
	else
	{
		return 0; // 默认回到平地段
	}
}

/**
 * @brief 计算场景强度系数（决定前向优待权重）
 * @param vzc 当前姿态的 Z 分量
 * @return 强度系数（0=不生效，1=完全生效）
 */
static float get_scene_strength(float vzc)
{
	if (func_abs(vzc) < VZC_VERTICAL_THRESH)
	{
		return 1.0f; // 竖直墙面：完全生效
	}
	else if (vzc > VZC_VERTICAL_THRESH && vzc < VZC_GROUND_THRESH)
	{
		// 正向缓坡面（下→左/右→下过渡）：强度随vzc增大而降低
		return 1.0f - (vzc - VZC_VERTICAL_THRESH) / (VZC_GROUND_THRESH - VZC_VERTICAL_THRESH);
	}
	else if (vzc > VZC_INVERT_THRESH && vzc < -VZC_VERTICAL_THRESH)
	{
		// 反向缓坡面（左→上/上→右过渡）：强度随vzc减小而降低
		return 1.0f - (-VZC_VERTICAL_THRESH - vzc) / (-VZC_VERTICAL_THRESH - VZC_INVERT_THRESH);
	}
	else
	{
		return 0.0f; // 平地/倒立顶点：不生效
	}
}

/**
 * @brief 计算前向优待修正值（在非平地场景提升“前向”吸附）
 * @param base_duty 当前阶段基础负压
 * @param vzc 当前姿态 Z 分量
 * @param vxc 当前姿态 X 分量（前向）
 * @param vyc 当前姿态 Y 分量（侧向）
 * @return 修正后的目标负压
 */
static float calculate_forward_preference(float base_duty, float vzc, float vxc, float vyc)
{
	// 1. 获取场景强度系数（0~1）
	float strength = get_scene_strength(vzc);
	float gt_norm;
	float align_fwd = 0.5f; // 默认中性
	float offset;

	if (strength < 0.1f)
	{
		return base_duty; // 强度过低，不修正
	}

	// 2. 计算平面内方向模长（避免除零）
	gt_norm = (float)sqrt(vxc * vxc + vyc * vyc);

	// 3. 计算朝向对齐度（与x轴对齐：0=横向，1=前向）
	if (gt_norm > 1e-6f)
	{
		align_fwd = (float)fabs(vxc) / gt_norm; // 兼容左右方向
		align_fwd = func_limit_ab(align_fwd, 0.0f, 1.0f);
	}

	// 4. 计算前向优待偏移（横向-250，前向+250，基于PREFERENCE_OFFSET）
	offset = (align_fwd - 0.5f) * PREFERENCE_OFFSET;

	// 5. 按场景强度叠加偏移（强度越高，偏移影响越大）
	return base_duty + strength * offset;
}

/**
 * @brief 计算当前阶段的基础目标负压
 * @param phase 阶段编号
 * @param vzc 当前姿态
 * @return 基础目标负压
 */
static float calculate_base_duty(uint8 phase, float vzc)
{
	float ratio;
	float delta;
	DUTY_BOTTOM = fuya_xili;

	switch (phase)
	{
	case 0: // 平地
		return DUTY_BOTTOM;
	case 1: // 平地上墙：vzc 1→0，指数型放大早期响应
		ratio = 1.0f - vzc;
		delta = 1.0f - (float)exp(-3.0f * ratio);
		return DUTY_BOTTOM + (DUTY_LEFT - DUTY_BOTTOM) * func_limit_ab(delta, 0.0f, 1.0f);
	case 2: // 墙上天花板：vzc 0→-1
		ratio = -vzc;
		delta = 1.0f - (float)exp(-3.0f * ratio);
		return DUTY_LEFT - (DUTY_LEFT - DUTY_TOP) * func_limit_ab(delta, 0.0f, 1.0f);
	case 3: // 天花板下墙：vzc -1→0
		ratio = vzc + 1.0f;
		delta = 1.0f - (float)exp(-3.0f * ratio);
		return DUTY_TOP + (DUTY_RIGHT - DUTY_TOP) * func_limit_ab(delta, 0.0f, 1.0f);
	case 4: // 墙下平地：vzc 0→1
		ratio = vzc;
		delta = 1.0f - (float)exp(-3.0f * ratio);
		return DUTY_RIGHT - (DUTY_RIGHT - DUTY_BOTTOM) * func_limit_ab(delta, 0.0f, 1.0f);
	default:
		return 1800.0f;
	}
}

/**
 * @brief 根据阶段选择平滑系数 alpha
 * @param phase 阶段编号
 * @return alpha值（0~1）
 */
static float get_alpha_by_phase(uint8 phase)
{
	switch (phase)
	{
	case 0:
		return ALPHA_UP1;
	case 1:
		return ALPHA_UP2;
	case 2:
		return ALPHA_DOWN1;
	case 3:
		return ALPHA_DOWN2;
	default:
		return ALPHA_DEFAULT;
	}
}

/**
 * @brief 简单版负压吸附占空比更新函数
 * @note 根据 IMU 解算得到的姿态向量，按五个阶段调节负压，并叠加前向优待：
 *       阶段 0：平地稳定段（vzc≈1，vxc≈0）
 *       阶段 1：平地 → 左墙（vzc:1→0，vxc:0→-1）
 *       阶段 2：左墙 → 天花板（vzc:0→-1，vxc:-1→0）
 *       阶段 3：天花板 → 右墙（vzc:-1→0，vxc:0→1）
 *       阶段 4：右墙 → 平地（vzc:0→1，vxc:1→0）
 *       同时根据前向优待策略，在非平地场景对目标负压做微调
 */
void fuya_update_simple(void)
{
	float vzc, vxc, vyc;
	float base_duty;
	float target_duty;
	float alpha;

	// 1. 传感器数据限幅（防异常）
	vzc = func_limit(vz, 1.0f);
	vxc = func_limit(vx, 1.0f);
	vyc = func_limit(vy, 1.0f);

	// 2. 判断当前阶段
	phase = get_current_phase(vz, vx);

	// 3. 计算阶段基础负压
	base_duty = calculate_base_duty(phase, vzc);

	// 4. 叠加前向优待（非平地场景差异化修正）
	target_duty = calculate_forward_preference(base_duty, vzc, vxc, vyc);

	// 6. 选择平滑系数（阶段差异化）
	alpha = get_alpha_by_phase(phase);

	// 7. 一阶低通滤波（平滑过渡，防突变）
	fuya_date = (int)(fuya_date + alpha * (target_duty - fuya_date));

	// 8. 保存目标占空比和当前vzc用于下一周期过渡判断
	fuya_date_factor = target_duty;

	// 9. 输出到风扇PWM
	fuya_motor_output(fuya_xili);
}