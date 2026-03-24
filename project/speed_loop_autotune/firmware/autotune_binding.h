#ifndef _AUTOTUNE_BINDING_H_
#define _AUTOTUNE_BINDING_H_

#include "speed_loop_autotune_private.h"

uint8 speed_loop_autotune_binding_missing_mask(const speed_loop_autotune_binding_t *binding);
uint8 speed_loop_autotune_binding_is_bound(const speed_loop_autotune_binding_t *binding, uint8 slot);
float speed_loop_autotune_binding_read(const speed_loop_autotune_binding_t *binding, uint8 slot);
uint8 speed_loop_autotune_binding_write(const speed_loop_autotune_binding_t *binding, uint8 slot, float value);
void speed_loop_autotune_binding_mirror_to_array(
    const speed_loop_autotune_binding_t *binding,
    float *values,
    uint8 count);
void speed_loop_autotune_binding_mirror_from_array(
    const speed_loop_autotune_binding_t *binding,
    const float *values,
    uint8 count);
const char *speed_loop_autotune_binding_slot_name(uint8 slot);

#endif /* _AUTOTUNE_BINDING_H_ */
