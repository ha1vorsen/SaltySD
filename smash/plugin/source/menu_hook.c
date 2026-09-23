#include "common.h"
#include "status.h"

#include "types.h"

#define CRO_MAGIC_OFFS      0x80
#define CRO_NAME_OFFS       0x84
#define CRO_CODE_OFFS       0xB0
#define CRO_CODE_SIZE_OFFS  0xB4
#define CRO_MAGIC_LOADED    0x304F5243u
#define CRO_MAGIC_FIXED     0x44584946u

#define STATE_NOTICES       0x6F
#define STATE_MAIN_MENU     0x3

#define SIG_WORDS           15
#define SIG_SITE_OFFS       0x2C
#define BL_MASK             0xFF000000u
#define BL_AL               0xEB000000u
#define IMPORT_VENEER       0xE51FF004u

static const u32 menu_sig[SIG_WORDS] = {
    0xE28400A0, BL_AL,      0xE58D0000, 0xE3A00003, 0xE58D0004, 0xE28D2004,
    0xE1A0100D, 0xE28400D0, BL_AL,      0xE5901000, 0xE2840058, BL_AL,
    0xE2840058, BL_AL,      0xE350004A,
};

void host_menu_run(void);

static int is_menu(const char *name)
{
    return name[0] == 'm' && name[1] == 'e' &&
           name[2] == 'n' && name[3] == 'u' && !name[4];
}

static int sig_at(const u32 *at)
{
    for (u32 i = 0; i < SIG_WORDS; i++) {
        u32 mask = menu_sig[i] == BL_AL ? BL_MASK : 0xFFFFFFFFu;
        if ((at[i] & mask) != menu_sig[i])
            return 0;
    }
    return 1;
}

static u32 bl_target(u32 site, u32 word)
{
    int displacement = (int)(word << 8) >> 8;
    return site + 8 + (u32)(displacement * 4);
}

static void set_menu_site(u32 base)
{
    saltysd_status.menu_site = 0;

    u32 code = *(u32 *)(base + CRO_CODE_OFFS);
    u32 size = *(u32 *)(base + CRO_CODE_SIZE_OFFS);
    if (size < SIG_WORDS * 4)
        return;

    u32 found = 0, matches = 0;
    for (u32 at = code; at <= code + size - SIG_WORDS * 4; at += 4) {
        if (sig_at((const u32 *)at)) {
            found = at;
            matches++;
        }
    }
    if (matches != 1)
        return;

    u32 site = found + SIG_SITE_OFFS;
    u32 before = *(const volatile u32 *)site;

    if ((before & BL_MASK) != BL_AL)
        return;

    u32 veneer = bl_target(site, before);
    if (veneer < code || veneer > code + size - 8)
        return;
    if (*(const volatile u32 *)veneer != IMPORT_VENEER)
        return;

    u32 target = *(const volatile u32 *)(veneer + 4) & ~1u;
    if (target != menu_hook_site_ADDR)
        return;

    saltysd_status.menu_site = site;
}

void *saltysd_cro_loaded(void *request)
{
    if (!request)
        return request;

    u32 status = *((u32 *)request + 2);
    if (status & 0x80000000u)
        return request;

    u32 base = *(u32 *)request;
    if (!base)
        return request;

    u32 magic = *(u32 *)(base + CRO_MAGIC_OFFS);
    if (magic != CRO_MAGIC_LOADED && magic != CRO_MAGIC_FIXED)
        return request;

    u32 name = *(u32 *)(base + CRO_NAME_OFFS);
    if (name < base)
        name += base;

    if (is_menu((const char *)name))
        set_menu_site(base);

    return request;
}

u32 saltysd_menu_state(u32 state, u32 caller)
{
    if (!saltysd_status.menu_site ||
        caller != saltysd_status.menu_site + 4 ||
        state != STATE_NOTICES)
        return state;

    saltysd_status.menu_opens++;
    host_menu_run();
    return STATE_MAIN_MENU;
}
