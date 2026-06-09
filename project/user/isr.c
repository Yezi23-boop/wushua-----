/**
 * @file isr.c
 * @brief AI8051U 中断服务入口实现
 * @details
 * 统一承接 UART DMA、中断引脚和 PIT 定时器中断，
 * 并在最小开销前提下把事件转发给业务层回调。
 *
 * 设计要求：
 * 1) 中断内只做必要清标志与轻量调度；
 * 2) 避免阻塞操作，保证高频控制环实时性；
 * 3) 通过编译开关精确控制 TM0/TM1 挂载任务。
 */
#include "zf_common_headfile.h"

/**
 * @brief UART1 DMA 接收中断
 * @details 负责程序自动下载逻辑检测及 UART1 原始数据分发
 */
void DMA_UART1_IRQHandler(void) interrupt 4
{
    static vuint8 dwon_count = 0;
    if (DMA_UR1R_STA & 0x01) /* 接收完成标志 */
    {
        DMA_UR1R_STA &= ~0x01;
        uart_rx_start_buff(UART_1); /* 准备下一次接收 */

        /* 原因：STC 单片机特定 ISP 在线下载时特征码序列。在正常运行时监测到该序列长驻则执行自我热重启跳转。 
 * 若误将正常通信当做烧录特征，可能引发运行中不预期掉电重启。 */
        if (uart_rx_buff[UART_1][0] == 0x7F)
        {
            if (dwon_count++ > 20)
                IAP_CONTR = 0x60;
        }
        else
            dwon_count = 0;

        /* 回调给外部上层注册的响应器（若非空），保障模块间的弱耦合 */
        if (uart1_irq_handler != NULL)
            uart1_irq_handler(uart_rx_buff[UART_1][0]);
    }

    if (DMA_UR1R_STA & 0x02) /* 数据溢出丢弃标志 */
    {
        DMA_UR1R_STA &= ~0x02;
        uart_rx_start_buff(UART_1);
    }
}

/**
 * @brief UART2 DMA 接收中断
 */
void DMA_UART2_IRQHandler(void) interrupt 8
{
    if (DMA_UR2R_STA & 0x01)
    {
        DMA_UR2R_STA &= ~0x01;
        uart_rx_start_buff(UART_2);
        if (uart2_irq_handler != NULL)
            uart2_irq_handler(uart_rx_buff[UART_2][0]);
    }
    if (DMA_UR2R_STA & 0x02)
    {
        DMA_UR2R_STA &= ~0x02;
        uart_rx_start_buff(UART_2);
    }
}

/**
 * @brief UART3 DMA 接收中断
 */
void DMA_UART3_IRQHandler(void) interrupt 17
{
    if (DMA_UR3R_STA & 0x01)
    {
        DMA_UR3R_STA &= ~0x01;
        uart_rx_start_buff(UART_3);
        if (uart3_irq_handler != NULL)
            uart3_irq_handler(uart_rx_buff[UART_3][0]);
    }
    if (DMA_UR3R_STA & 0x02)
    {
        DMA_UR3R_STA &= ~0x02;
        uart_rx_start_buff(UART_3);
    }
}

/**
 * @brief UART4 DMA 接收中断
 */
void DMA_UART4_IRQHandler(void) interrupt 18
{
    if (DMA_UR4R_STA & 0x01)
    {
        DMA_UR4R_STA &= ~0x01;
        uart_rx_start_buff(UART_4);
        if (uart4_irq_handler != NULL)
            uart4_irq_handler(uart_rx_buff[UART_4][0]);
    }
    if (DMA_UR4R_STA & 0x02)
    {
        DMA_UR4R_STA &= ~0x02;
        uart_rx_start_buff(UART_4);
    }
}

/**
 * @brief 外部中断1
 * @details 供 IMU660RC 的 INT1(P33) 回调使用
 */
void INT1_IRQHandler(void) interrupt 2
{
    INT1_CLEAR_FLAG;
    if (int1_irq_handler != NULL)
        int1_irq_handler();
}

/**
 * @brief PIT0 定时器中断 (系统控制环中断)
 * @details 触发核心控制环任务 run_time_1，周期见 int_user.c 的 TIME_0 定义（通常为 2ms）。
 * 注意：此函数必须保持极低耗时，不可执行阻塞操作。
 */
void TM0_IRQHandler() interrupt 1
{
    TIM0_CLEAR_FLAG;
// a_run_apply_iap_guard();
///* 1. 获取编码器实时速度反馈 */
// Encoder_get(&PID.left_speed, &PID.right_speed);
// motor_output(3000, 4000);
#if MAIN_ENABLE_ISR_TEST_DIFF_FUNC
    test_diff_func();
#endif
#if MAIN_ENABLE_ISR_TEST_SPEED_FUNC
    test_speed_func();
#endif
#if MAIN_ENABLE_ISR_RUN_TIME_1
    run_time_1(); /* 执行核心控制逻辑 */
#endif
    if (tim0_irq_handler != NULL)
        tim0_irq_handler();
}

/**
 * @brief PIT1 定时器中断 (系统管理中断)
 * @details 触发系统管理任务 run_time_2，周期见 int_user.c 的 TIME_1 定义（通常为 10ms）。
 */
void TM1_IRQHandler() interrupt 3
{
    TIM1_CLEAR_FLAG;
#if MAIN_ENABLE_ISR_RUN_TIME_2
    run_time_2(); /* 执行系统状态管理逻辑 */
#endif
    if (tim1_irq_handler != NULL)
        tim1_irq_handler();
}

/**
 * @brief PIT2 定时器中断
 */
void TM2_IRQHandler() interrupt 12
{
    TIM2_CLEAR_FLAG;
    if (tim2_irq_handler != NULL)
        tim2_irq_handler();
}

/**
 * @brief PIT3 定时器中断
 */
void TM3_IRQHandler() interrupt 19
{
    TIM3_CLEAR_FLAG;
    if (tim3_irq_handler != NULL)
        tim3_irq_handler();
}

/**
 * @brief PIT4 定时器中断
 */
void TM4_IRQHandler() interrupt 20
{
    TIM4_CLEAR_FLAG;
    if (tim4_irq_handler != NULL)
        tim4_irq_handler();
}

/**
 * @brief PIT11 定时器中断
 */
void TM11_IRQHandler() interrupt 24
{
    TIM11_CLEAR_FLAG;
    if (tim11_irq_handler != NULL)
        tim11_irq_handler();
}
