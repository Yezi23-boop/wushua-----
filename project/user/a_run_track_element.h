#ifndef __A_RUN_TRACK_ELEMENT_H__
#define __A_RUN_TRACK_ELEMENT_H__

#include "zf_common_typedef.h"
#include "track_element_config.h"

/**
 * @brief 更新赛道元素仲裁状态机。
 *
 * 5ms 主控制链路直接调用该接口，避免高频路径多一层只转发的包装。
 */
void a_run_track_element_update_gate(void);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶，4-墙面，5-跷跷板。
 */
int8 a_run_track_element_get_expected_element(void);

#endif /* __A_RUN_TRACK_ELEMENT_H__ */
