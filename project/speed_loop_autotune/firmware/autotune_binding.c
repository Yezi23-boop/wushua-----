#include "zf_common_headfile.h"
#include "speed_loop_autotune_private.h"

static const char *const speed_loop_autotune_binding_slot_names[SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT] = {
    "LEFT_KP",
    "LEFT_KI",
    "LEFT_KD",
    "RIGHT_KP",
    "RIGHT_KI",
    "RIGHT_KD",
};

void speed_loop_autotune_binding_init(speed_loop_autotune_binding_t *binding)
{
    uint8 index;

    if (binding == 0)
    {
        return;
    }

    for (index = 0; index < SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT; index++)
    {
        binding->slots[index].name = speed_loop_autotune_binding_slot_names[index];
        binding->slots[index].slot_addr = 0;
    }
}

void speed_loop_autotune_binding_bind(
    speed_loop_autotune_binding_t *binding,
    uint8 slot,
    float *slot_addr,
    const char *name)
{
    if (binding == 0 || slot >= SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT)
    {
        return;
    }

    binding->slots[slot].slot_addr = slot_addr;
    if (name != 0)
    {
        binding->slots[slot].name = name;
    }
}

uint8 speed_loop_autotune_binding_missing_mask(const speed_loop_autotune_binding_t *binding)
{
    uint8 index;
    uint8 missing_mask;

    if (binding == 0)
    {
        return (uint8)0xFFu;
    }

    missing_mask = 0;
    for (index = 0; index < SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT; index++)
    {
        if (binding->slots[index].slot_addr == 0)
        {
            missing_mask |= (uint8)(1u << index);
        }
    }
    return missing_mask;
}

uint8 speed_loop_autotune_binding_check(const speed_loop_autotune_binding_t *binding)
{
    return (uint8)(speed_loop_autotune_binding_missing_mask(binding) == 0u);
}

uint8 speed_loop_autotune_binding_is_bound(const speed_loop_autotune_binding_t *binding, uint8 slot)
{
    if (binding == 0 || slot >= SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT)
    {
        return 0;
    }
    return (uint8)(binding->slots[slot].slot_addr != 0);
}

float speed_loop_autotune_binding_read(const speed_loop_autotune_binding_t *binding, uint8 slot)
{
    if (!speed_loop_autotune_binding_is_bound(binding, slot))
    {
        return 0.0f;
    }
    return *(binding->slots[slot].slot_addr);
}

uint8 speed_loop_autotune_binding_write(const speed_loop_autotune_binding_t *binding, uint8 slot, float value)
{
    if (!speed_loop_autotune_binding_is_bound(binding, slot))
    {
        return 0;
    }

    *(binding->slots[slot].slot_addr) = value;
    return 1;
}

void speed_loop_autotune_binding_mirror_to_array(
    const speed_loop_autotune_binding_t *binding,
    float *values,
    uint8 count)
{
    uint8 index;

    if (binding == 0 || values == 0)
    {
        return;
    }

    for (index = 0; index < SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT && index < count; index++)
    {
        values[index] = speed_loop_autotune_binding_read(binding, index);
    }
}

void speed_loop_autotune_binding_mirror_from_array(
    const speed_loop_autotune_binding_t *binding,
    const float *values,
    uint8 count)
{
    uint8 index;

    if (binding == 0 || values == 0)
    {
        return;
    }

    for (index = 0; index < SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT && index < count; index++)
    {
        speed_loop_autotune_binding_write(binding, index, values[index]);
    }
}

const char *speed_loop_autotune_binding_slot_name(uint8 slot)
{
    if (slot >= SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT)
    {
        return "UNKNOWN";
    }
    return speed_loop_autotune_binding_slot_names[slot];
}
