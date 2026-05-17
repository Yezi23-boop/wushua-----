#ifndef __TRACK_ELEMENT_CONFIG_H__
#define __TRACK_ELEMENT_CONFIG_H__

#include "zf_common_typedef.h"

#define TRACK_ELEMENT_NONE 0       /**< 空槽位，用于跳过或现场临时关闭某个序列位置。 */
#define TRACK_ELEMENT_LEFT_RING 1  /**< 左圆环流程显示值。 */
#define TRACK_ELEMENT_RIGHT_RING 2 /**< 右圆环流程显示值。 */
#define TRACK_ELEMENT_CYLINDER 3   /**< 圆桶流程显示值。 */
#define TRACK_ELEMENT_WALL 4       /**< 墙面流程显示值。 */
#define TRACK_ELEMENT_SEESAW 5     /**< 跷跷板流程显示值。 */

#define TRACK_ELEMENT_SEQUENCE_MAX 6 /**< 菜单和 EEPROM 固定保存的最大元素槽位数。 */
#define TRACK_ELEMENT_DEFAULT_LEN 4  /**< 默认有效长度：左环、圆桶、跷跷板、墙面。 */
#define TRACK_ELEMENT_DEFAULT_0 TRACK_ELEMENT_LEFT_RING
#define TRACK_ELEMENT_DEFAULT_1 TRACK_ELEMENT_CYLINDER
#define TRACK_ELEMENT_DEFAULT_2 TRACK_ELEMENT_SEESAW
#define TRACK_ELEMENT_DEFAULT_3 TRACK_ELEMENT_WALL
#define TRACK_ELEMENT_DEFAULT_4 TRACK_ELEMENT_NONE
#define TRACK_ELEMENT_DEFAULT_5 TRACK_ELEMENT_NONE

#endif /* __TRACK_ELEMENT_CONFIG_H__ */
