#include "melee_airdodge.h"

typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3;

enum {
    COMMAND_OFFSET = 0x78,
    COMMAND_ESCAPE_AIR = 0x400000,
    STICK_Y_OFFSET = 0x98,
    STICK_X_OFFSET = 0xD8,
    STATUS_ESCAPE_AIR = 0x22,
    KINETIC_OFFSET = 0x7000,
    KINETIC_CURRENT = 0x110,
    KINETIC_ENTRIES = 0x14,
    ENERGY_VERTICAL = 0x10,
    ENERGY_HORIZONTAL = 0x20,
    STATUS_TABLE = 0x00D4B900,
    STATUS_TABLE_COUNT = 8,
    RING_HEAD = 0x00D4B5F0,
    RING = 0x00D4B600,
    VELOCITY_SLOT = 0x00D4B940,
};

typedef struct { void *fighter; uint32_t status; } status_entry;
typedef struct { void *module; void *fighter; float x; float y; } velocity_slot;
typedef struct { void *fighter; uint32_t status; uint32_t previous_frames; uint32_t caller; } ring_entry;

extern void reset_enable_energy(uint32_t id, void *boma, uint32_t flags,
                                const vec2 *speed, const vec3 *unused);

#define CAVE(name) __attribute__((section(name), noinline))

static inline void *pointer_at(const void *base, uint32_t offset)
{
    return *(void *const *)((const uint8_t *)base + offset);
}

static inline uint32_t *word_at(void *base, uint32_t offset)
{
    return (uint32_t *)((uint8_t *)base + offset);
}

static inline float *float_at(void *base, uint32_t offset)
{
    return (float *)((uint8_t *)base + offset);
}

static inline float absolute(float value)
{
    return value < 0.0f ? -value : value;
}

static inline void *kinetic_module(void *fighter)
{
    void *container_a = pointer_at(fighter, 8);
    void *container_b = pointer_at(container_a, 0);
    void *kinetic_container = pointer_at(container_b, 4);
    return pointer_at(kinetic_container, KINETIC_OFFSET + KINETIC_CURRENT);
}

static inline status_entry *status_table(void)
{
    return (status_entry *)(uintptr_t)STATUS_TABLE;
}

static inline uint32_t current_status(void *fighter)
{
    status_entry *entries = status_table();
    for (uint32_t i = 0; i != STATUS_TABLE_COUNT; ++i)
        if (entries[i].fighter == fighter)
            return entries[i].status;
    return 0;
}

CAVE(".cave_logic") void melee_seed(void *fighter)
{
    void *module = kinetic_module(fighter);
    void *boma = pointer_at(module, 4);
    vec2 zero2 = { 0.0f, 0.0f };
    vec3 zero3 = { 0.0f, 0.0f, 0.0f };
    float x = *float_at(fighter, STICK_X_OFFSET);
    float y = *float_at(fighter, STICK_Y_OFFSET);

    reset_enable_energy(0, boma, 4, &zero2, &zero3);
    if (absolute(x) < 0.20f && absolute(y) < 0.20f) {
        x = 0.0f;
        y = 0.0f;
    } else {
        float length = __builtin_sqrtf(x * x + y * y);
        float scale = 3.10f / length;
        x *= scale;
        y *= scale;
    }

    vec2 vertical = { 0.0f, y };
    vec2 horizontal = { x, 0.0f };
    reset_enable_energy(1, boma, 0, &vertical, &zero3);
    reset_enable_energy(2, boma, 4, &horizontal, &zero3);

    velocity_slot *slot = (velocity_slot *)(uintptr_t)VELOCITY_SLOT;
    slot->module = module;
    slot->fighter = fighter;
    slot->x = x;
    slot->y = y;
}

CAVE(".cave_logic") void melee_decay(void *fighter)
{
    velocity_slot *slot = (velocity_slot *)(uintptr_t)VELOCITY_SLOT;
    *word_at(fighter, COMMAND_OFFSET) &= ~COMMAND_ESCAPE_AIR;
    slot->x *= 0.90f;
    slot->y *= 0.90f;
    if (absolute(slot->x) < 0.20f) slot->x = 0.0f;
    if (absolute(slot->y) < 0.20f) slot->y = 0.0f;

    void *entries = pointer_at(kinetic_module(fighter), KINETIC_ENTRIES);
    void *vertical = pointer_at(entries, ENERGY_VERTICAL);
    void *horizontal = pointer_at(entries, ENERGY_HORIZONTAL);
    *float_at(vertical, 8) = slot->y;
    *float_at(horizontal, 4) = slot->x;
}

CAVE(".cave_logic") void melee_status_record(void *fighter, uint32_t status,
                                                 uint32_t caller)
{
    volatile uint32_t *head = (uint32_t *)(uintptr_t)RING_HEAD;
    ring_entry *ring = (ring_entry *)(uintptr_t)RING;
    uint32_t index = (*head)++ & 0x1f;
    uint32_t *frame_pointer = (uint32_t *)pointer_at(fighter, 0x4f0);
    ring[index] = (ring_entry){ fighter, status,
        frame_pointer ? *frame_pointer : UINT32_MAX, caller };

    status_entry *entries = status_table();
    for (uint32_t i = 0; i != STATUS_TABLE_COUNT; ++i) {
        if (entries[i].fighter == fighter || entries[i].fighter == 0) {
            entries[i] = (status_entry){ fighter, status };
            return;
        }
    }
}

CAVE(".cave_logic") int melee_block_redodge(void *fighter)
{
    return current_status(fighter) == STATUS_ESCAPE_AIR;
}

CAVE(".cave_logic") int melee_override_velocity(void *module, float *out_xy)
{
    velocity_slot *slot = (velocity_slot *)(uintptr_t)VELOCITY_SLOT;
    if (slot->module != module || current_status(slot->fighter) != STATUS_ESCAPE_AIR)
        return 0;
    out_xy[0] = slot->x;
    out_xy[1] = slot->y;
    return 1;
}

CAVE(".cave_logic") int melee_landing_only(void *fighter)
{
    return current_status(fighter) == STATUS_ESCAPE_AIR;
}
