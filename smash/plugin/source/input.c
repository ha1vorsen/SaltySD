#include "common.h"
#include "input.h"

#include "types.h"

#define HID_PAD_OFFS    0x4
#define PAD_INDEX_WORD  4
#define PAD_ENTRY_WORD  10
#define PAD_ENTRY_WORDS 4
#define PAD_LAST_INDEX  7

static volatile u32 *pad;

int input_open(void)
{
    pad = *(volatile u32 **)(hid_object_ADDR + HID_PAD_OFFS);
    return pad != 0;
}

unsigned int input_held(void)
{
    u32 index = pad[PAD_INDEX_WORD];
    if (index > PAD_LAST_INDEX)
        index = PAD_LAST_INDEX;
    return pad[PAD_ENTRY_WORD + index * PAD_ENTRY_WORDS];
}
