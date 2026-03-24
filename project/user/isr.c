#include "zf_common_headfile.h"
#include "../speed_loop_autotune/firmware/speed_loop_trial.h"

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

        /* 程序自动下载逻辑：检测连续的 0x7F 特征码 */
        if (uart_rx_buff[UART_1][0] == 0x7F)
        {
            if (dwon_count++ > 20)
                IAP_CONTR = 0x60;
        }
        else
            dwon_count = 0;

        /* 调用用户自定义串口回调 */
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
 * @brief PIT0 定时器中断 (5ms)
 * @details 触发核心控制环任务 run_time_1
 */
void TM0_IRQHandler() interrupt 1
{
    TIM0_CLEAR_FLAG;
#if MAIN_ENABLE_ISR_RUN_TEST_SPEED
    run_test_speed();
#endif
#if MAIN_ENABLE_ISR_TEST_ANGLE_FUNC
    test_angle_func();
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
 * @brief PIT1 定时器中断 (10ms)
 * @details 触发系统管理任务 run_time_2
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
