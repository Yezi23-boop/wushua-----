#include "zf_common_headfile.h"

/**
 * @brief 程序主入口
 */
void main()
{
        /* 1. 系统时钟初始化 (设置为 40MHz，兼顾性能与功耗) */
        clock_init(SYSTEM_CLOCK_40M);

        /* 2. 库函数内部调试接口初始化 */
        debug_init();

        /* 3. 硬件强制复位引脚配置 */
        P32 = 1;

        /* 4. 调用用户层总初始化 (包含所有硬件、控制环、PID 的初始化) */
        int_user();
        Menu_Set_Service_Enable((uint8)MAIN_ENABLE_MENU);

        /* 5. 守护死循环，处理无法放入严格时间轴断点的底级UI和后台监控通信包，
   因无法保证时序确定性，不能在此处安放赛道跟踪高频控制算法 */
        while (1)
        {
                /* A. 处理 VOFA+ 指令解析与数据上报 */
#if MAIN_ENABLE_VOFA
                debug_vofa_service();
#endif

                /* B. 处理 IPS 屏幕菜单渲染与按键交互 (参数修改核心) */
#if MAIN_ENABLE_MENU
                //	P36=0;
                Keystroke_Menu();
                //	P36=1;
#endif

                /* C. 处理速度测试数据的定时打印 */
#if MAIN_ENABLE_SPEED_TEST

//			printf("%f,%f,%f,%f\n", PID.left_speed.speed, PID.right_speed.speed, P46, speed_r);
			ips114_show_int32(4 * 24, 18 * 0,P36, 4);
//			P43=0;
						ips114_show_float(4 * 24, 18 * 0,PID.left_speed.speed, 4, 1);
			ips114_show_float(4 * 24, 18 * 1,PID.right_speed.speed, 4, 1);
//			system_delay_ms(200);
//          printf_imu();
//			printf_adc();
//			printf_speed_test();
#endif
        }
}
