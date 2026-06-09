/**
 * @file speed_loop_autotune_adapter.c
 * @brief 速度环自动调参适配入口占位实现
 * @details
 * 当前仓库未包含 `project/speed_loop_autotune/` 专题目录，旧版自动调参固件组件
 * 不再参与 Keil 工程构建。保留这个入口仅用于兼容历史调用点，避免误 include
 * 一个不存在的专题头文件。
 */
#include "zf_common_headfile.h"
#include "speed_loop_autotune_adapter.h"

/**
 * @brief 初始化速度环自动调参适配层
 * @details
 * 自动调参专题目录未随当前工程提供，本函数保持空实现。若后续恢复该专题，
 * 应重新接入真实 binding/port 初始化，并同步更新 Keil 工程和 VOFA 文档。
 */
void speed_loop_autotune_project_init(void)
{
}
