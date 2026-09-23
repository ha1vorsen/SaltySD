#include "common.h"
#include "display.h"

#include "types.h"

#define GSP_CLIENT_OFFS      0x1C
#define GSP_TOP_INFO_OFFS    0x5C
#define GSP_BOTTOM_INFO_OFFS 0x60

#define FB_ENTRY_OFFS        0x4
#define FB_ENTRY_SIZE        0x1C
#define FB_LEFT_OFFS         0x4
#define FB_RIGHT_OFFS        0x8
#define FB_STRIDE_OFFS       0xC
#define FB_FORMAT_OFFS       0x10

#define FB_SLOT_MASK         0xFFu
#define FB_UPDATE_MASK       0xFF00u
#define FB_UPDATE            0x100u

static int read_frame(const display_screen *source, u32 slot, u32 header,
                      display_frame *out)
{
    volatile u8 *entry = (volatile u8 *)source->header + FB_ENTRY_OFFS +
                         slot * FB_ENTRY_SIZE;

    out->left = *(u8 *volatile *)(entry + FB_LEFT_OFFS);
    out->right = *(u8 *volatile *)(entry + FB_RIGHT_OFFS);
    out->layout.stride = *(volatile u32 *)(entry + FB_STRIDE_OFFS);
    out->layout.format = *(volatile u32 *)(entry + FB_FORMAT_OFFS) & 7;
    out->layout.width = source->width;
    out->slot = slot;
    out->snapshot = header;

    if (out->right == out->left)
        out->right = 0;
    return out->left && out->layout.stride && out->layout.format <= FMT_RGBA4;
}

static int begin_screen(const display_screen *source, display_frame *out)
{
    u32 before = *source->header;
    if (before & FB_UPDATE_MASK)
        return 0;

    //GSP writes the next presentation into the slot opposite the last one.
    //Prefer that slot so the displayed descriptor is not rewritten.
    u32 slot = 1 - (before & 1);
    if (!read_frame(source, slot, before, out)) {
        slot ^= 1;
        if (!read_frame(source, slot, before, out))
            return 0;
    }

    //A producer may have published while the descriptor was copied.
    return *source->header == before;
}

static int publish_screen(const display_screen *source, const display_frame *frame)
{
    volatile u32 *at = source->header;
    u32 old, next, failed, barrier = 0;

    //ARMv6 synchronization barrier; dsb is unavailable for -march=armv6k.
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(barrier) : "memory");
    for (;;) {
        __asm__ volatile("ldrex %0, [%1]" : "=&r"(old) : "r"(at) : "memory");
        if ((old & (FB_SLOT_MASK | FB_UPDATE_MASK)) !=
            (frame->snapshot & FB_SLOT_MASK)) {
            __asm__ volatile("clrex" ::: "memory");
            return 0;
        }

        next = (old & ~(FB_SLOT_MASK | FB_UPDATE_MASK)) |
               frame->slot | FB_UPDATE;
        __asm__ volatile("strex %0, %2, [%1]"
                         : "=&r"(failed) : "r"(at), "r"(next) : "memory");
        if (!failed)
            return 1;
    }
}

static int open_screen(display_screen *out, u32 info, u32 width)
{
    display_frame frame;
    if (!info)
        return 0;

    out->header = (volatile u32 *)info;
    out->width = width;

    u32 header = *out->header;
    return read_frame(out, header & 1, header, &frame) ||
           read_frame(out, 1 - (header & 1), header, &frame);
}

int display_open(display *out)
{
    u32 client = *(volatile u32 *)(gsp_state_ADDR + GSP_CLIENT_OFFS);
    if (!client)
        return 0;

    out->has_top = open_screen(&out->top,
                               *(volatile u32 *)(client + GSP_TOP_INFO_OFFS),
                               TOP_WIDTH);
    out->has_bottom = open_screen(&out->bottom,
                                  *(volatile u32 *)(client + GSP_BOTTOM_INFO_OFFS),
                                  BOTTOM_WIDTH);
    return out->has_top || out->has_bottom;
}

int display_begin(const display *d, display_frame *top, display_frame *bottom)
{
    if (d->has_top && !begin_screen(&d->top, top))
        return 0;
    if (d->has_bottom && !begin_screen(&d->bottom, bottom))
        return 0;
    return 1;
}

int display_present(const display *d, const display_frame *top,
                    const display_frame *bottom)
{
    //Publish only after every CPU store to the target buffers is visible.
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
    if (d->has_top && !publish_screen(&d->top, top))
        return 0;
    if (d->has_bottom && !publish_screen(&d->bottom, bottom))
        return 0;
    return 1;
}
