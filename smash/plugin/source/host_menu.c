#include "common.h"
#include "status.h"
#include "draw.h"
#include "display.h"
#include "fs.h"
#include "mods.h"
#include "update.h"
#include "version.h"
#include "input.h"

#include "types.h"

#define APPLET_CLOSE_A       0x06
#define APPLET_CLOSE_B       0x09
#define APPLET_SLEEP_REPLY   0x0B
#define APPLET_HOME_A        0x0C
#define APPLET_HOME_B        0x0D
#define SLEEP_REPLY_LATER    1

#define POLL_NS              16000000ull
#define SETTLE_FRAMES        8
#define RESTART_DELAY_NS     1000000000ull
#define PROGRESS_STEP        0x4000
#define TEXT_LINE_CAPACITY   64
#define TEXT_LINE_MAX        63

#define BG_RGB               0x10141C
#define TEXT_RGB             0xE0E0E0
#define TITLE_RGB            0x40C0C0
#define CURSOR_RGB           0xFFD040
#define DIM_RGB              0x808890

#define MOD_ROWS             20
#define MOD_ROW_Y            24
#define MOD_ROW_H            10
#define MOD_NAME_SHOWN       40

enum { EXIT_NONE, EXIT_BACK, EXIT_HOME, EXIT_SLEEP, EXIT_CLOSE };
enum { ITEM_MODS, ITEM_REBUILD, ITEM_STABLE, ITEM_DIRTY, ITEM_BACK, ITEM_COUNT };
enum { VIEW_MAIN, VIEW_MODS, VIEW_CONFIRM, VIEW_BUSY, VIEW_CODE, VIEW_OFFER,
       VIEW_INSTALL_CONFIRM, VIEW_REBUILD, VIEW_COUNT };

static const char *const item_names[ITEM_COUNT] = {
    "Mods", "Rebuild mod index", "Check for updates (stable)",
    "Check for updates (dirty)", "Back",
};

typedef struct {
    display output;
    u32 view;
    u32 cursor;
    u32 mod_cursor;
    u32 mod_first;
    char code[4];
    u32 code_pos;
    const char *status;
    const char *status2;
} menu;

static int (*const restart_app)(const void *param, u32 size) = (void *)restart_app_ADDR;

static char title_buf[TEXT_LINE_CAPACITY];
static char status_buf[TEXT_LINE_CAPACITY];
static char status2_buf[TEXT_LINE_CAPACITY];
static update_check last_check;

typedef struct {
    char *buf;
    u32 len;
} text_buffer;

static void put_str(text_buffer *line_text, const char *s)
{
    while (*s && line_text->len < TEXT_LINE_MAX)
        line_text->buf[line_text->len++] = *s++;
    line_text->buf[line_text->len] = 0;
}

static void put_dec(text_buffer *line_text, u32 v)
{
    char digits[10];
    u32 n = 0;
    do {
        digits[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (n && line_text->len < TEXT_LINE_MAX)
        line_text->buf[line_text->len++] = digits[--n];
    line_text->buf[line_text->len] = 0;
}

static void put_hex(text_buffer *line_text, u32 v)
{
    put_str(line_text, "0x");
    for (int shift = 28; shift >= 0 && line_text->len < TEXT_LINE_MAX; shift -= 4)
        line_text->buf[line_text->len++] = "0123456789ABCDEF"[(v >> shift) & 0xF];
    line_text->buf[line_text->len] = 0;
}

//max is the width the caller can draw, not a name length: the mark has to
//fit inside it or it lands past the edge of the screen and is never seen.
static void put_name(text_buffer *line_text, const unsigned short *name, u32 max)
{
    u32 len = 0;
    while (name[len])
        len++;

    u32 show = len;
    if (len > max)
        show = max > 3 ? max - 3 : 0;

    for (u32 i = 0; i < show && line_text->len < TEXT_LINE_MAX; i++)
        line_text->buf[line_text->len++] = name[i] >= 0x20 && name[i] <= 0x7E ? (char)name[i] : '?';
    line_text->buf[line_text->len] = 0;
    if (len > max)
        put_str(line_text, "...");
}

static void sleep_ns(u64 ns)
{
    //SleepThread takes the nanosecond duration in r0:r1.
    register u32 lo __asm__("r0") = (u32)ns;
    register u32 hi __asm__("r1") = (u32)(ns >> 32);
    __asm__ volatile("svc 0x0A" : "+r"(lo), "+r"(hi) :: "r2", "r3", "r12", "memory");
}

static u32 applet_pending(void)
{
    volatile u8 *state = (volatile u8 *)applet_state_ADDR;
    if (state[APPLET_HOME_A] || state[APPLET_HOME_B])
        return EXIT_HOME;
    if (state[APPLET_SLEEP_REPLY] == SLEEP_REPLY_LATER)
        return EXIT_SLEEP;
    if (state[APPLET_CLOSE_A] || state[APPLET_CLOSE_B])
        return EXIT_CLOSE;
    return EXIT_NONE;
}

static void set_status(menu *m, const char *line, const char *line2)
{
    m->status = line;
    m->status2 = line2;
}

static int draw(const menu *m);

static void paint_main(u8 *fb, const screen *s, const menu *m)
{
    draw_text(fb, s, 8, 8, title_buf, TITLE_RGB);
    for (u32 i = 0; i < ITEM_COUNT; i++) {
        int on = i == m->cursor;
        draw_text(fb, s, 8, 32 + i * 12, on ? ">" : " ", CURSOR_RGB);
        draw_text(fb, s, 24, 32 + i * 12, item_names[i], on ? CURSOR_RGB : TEXT_RGB);
    }
}

static void paint_mods(u8 *fb, const screen *s, const menu *m)
{
    char line[TEXT_LINE_CAPACITY];
    text_buffer line_text = { line, 0 };
    put_str(&line_text, "Mods ");
    if (mods_count > MOD_ROWS) {
        u32 last = m->mod_first + MOD_ROWS;
        put_dec(&line_text, m->mod_first + 1);
        put_str(&line_text, "-");
        put_dec(&line_text, last > mods_count ? mods_count : last);
        put_str(&line_text, " of ");
        put_dec(&line_text, mods_count);
    } else {
        put_str(&line_text, "(");
        put_dec(&line_text, mods_count);
        put_str(&line_text, ")");
    }
    draw_text(fb, s, 8, 8, line, TITLE_RGB);

    if (!mods_count)
        draw_text(fb, s, 8, MOD_ROW_Y, "No mod folders in /saltysd/smash", DIM_RGB);

    for (u32 row = 0; row < MOD_ROWS && m->mod_first + row < mods_count; row++) {
        u32 i = m->mod_first + row;
        const mod_entry *mod = &mods[i];
        int on = i == m->mod_cursor;
        u32 y = MOD_ROW_Y + row * MOD_ROW_H;
        u32 rgb = on ? CURSOR_RGB : mod->wanted ? TEXT_RGB : DIM_RGB;

        if (mod->wanted != mod->enabled)
            draw_text(fb, s, 8, y, "*", CURSOR_RGB);
        if (on)
            draw_text(fb, s, 16, y, ">", CURSOR_RGB);
        draw_text(fb, s, mod->wanted ? 40 : 32, y, mod->wanted ? "[On]" : "[Off]", rgb);

        line_text.len = 0;
        put_name(&line_text, mod->name, MOD_NAME_SHOWN);
        draw_text(fb, s, 80, y, line, rgb);
    }
}

static void paint_code(u8 *fb, const screen *s, const menu *m)
{
    draw_text(fb, s, 8, 8, "Nightly builds", TITLE_RGB);
    draw_text(fb, s, 8, 32, "Enter today's code:", TEXT_RGB);
    for (u32 i = 0; i < 4; i++) {
        char digit[2] = { m->code[i], 0 };
        int on = i == m->code_pos;
        draw_text(fb, s, 24 + i * 16, 52, digit, on ? CURSOR_RGB : TEXT_RGB);
        if (on)
            draw_text(fb, s, 24 + i * 16, 62, "^", CURSOR_RGB);
    }
}

//The top screen would otherwise keep showing the menu behind a blocking step.
static void paint_busy(u8 *fb, const screen *s, const menu *m)
{
    draw_text(fb, s, 8, 8, title_buf, TITLE_RGB);
    draw_text(fb, s, 8, 32, m->status ? m->status : "Working...", TEXT_RGB);
    if (m->status2)
        draw_text(fb, s, 8, 44, m->status2, DIM_RGB);
}

static void hint_main(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "Up/Down: Highlight  A: Select  B: Back", DIM_RGB);
}

static void hint_mods(u8 *fb, const screen *s, const menu *m)
{
    char line[TEXT_LINE_CAPACITY];
    text_buffer line_text = { line, 0 };

    (void)m;
    draw_text(fb, s, 8, 8, "Up/Down: Move  A: On/Off", DIM_RGB);
    draw_text(fb, s, 8, 20, "START: Apply  B: Back/Discard", DIM_RGB);
    put_dec(&line_text, mods_changes());
    put_str(&line_text, " Change(s) not applied");
    draw_text(fb, s, 8, 44, line, TEXT_RGB);
}

static void hint_confirm(u8 *fb, const screen *s, const menu *m)
{
    char line[TEXT_LINE_CAPACITY];
    text_buffer line_text = { line, 0 };

    (void)m;
    put_str(&line_text, "Write ");
    put_dec(&line_text, mods_changes());
    put_str(&line_text, " change(s) and restart?");
    draw_text(fb, s, 8, 8, line, TEXT_RGB);
    draw_text(fb, s, 8, 20, "A: Yes  B: No", CURSOR_RGB);
}

static void hint_code(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "Up/Down: Digit  Left/Right: Move", DIM_RGB);
    draw_text(fb, s, 8, 20, "A: Check  B: Back", DIM_RGB);
}

static void hint_offer(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "A: Install  B: Back", CURSOR_RGB);
}

static void hint_install_confirm(u8 *fb, const screen *s, const menu *m)
{
    char line[TEXT_LINE_CAPACITY];
    text_buffer line_text = { line, 0 };

    (void)m;
    put_str(&line_text, "Install ");
    put_str(&line_text, last_check.identity);
    put_str(&line_text, " and restart?");
    draw_text(fb, s, 8, 8, line, TEXT_RGB);
    draw_text(fb, s, 8, 20, "A: Yes  B: No", CURSOR_RGB);
}

static void restart(menu *m)
{
    set_status(m, 0, 0);
    m->view = VIEW_BUSY;
    draw(m);
    sleep_ns(RESTART_DELAY_NS);

    fs_close();
    int res = restart_app(0, 0);
    saltysd_status.restart_result = res;

    text_buffer line_text = { status_buf, 0 };
    put_str(&line_text, "Restart failed: ");
    put_hex(&line_text, (u32)res);
    set_status(m, status_buf, 0);
}

static void fs_failed(menu *m, const char *what, int res)
{
    text_buffer line_text = { status_buf, 0 };
    put_str(&line_text, what);
    put_str(&line_text, " failed: ");
    put_hex(&line_text, (u32)res);
    saltysd_status.last_fs_result = res;
    set_status(m, status_buf, 0);
}

static void open_mods(menu *m)
{
    int res = fs_open();
    if (res < 0) {
        fs_failed(m, "SD access", res);
        return;
    }
    res = mods_load();
    if (res < 0) {
        fs_close();
        fs_failed(m, "Reading /saltysd/smash", res);
        return;
    }

    m->view = VIEW_MODS;
    m->mod_cursor = m->mod_first = 0;
    set_status(m, 0, 0);
    if (mods_skipped) {
        text_buffer line_text = { status_buf, 0 };
        put_dec(&line_text, mods_skipped);
        put_str(&line_text, " folder(s) not shown: name too long or over 62");
        set_status(m, status_buf, 0);
    }
}

static void apply_mods(menu *m)
{
    mods_apply_result r;
    mods_apply(&r);

    text_buffer line_text = { status_buf, 0 };
    put_str(&line_text, "Applied ");
    put_dec(&line_text, r.applied);
    set_status(m, status_buf, 0);
    if (r.failed) {
        put_str(&line_text, ", failed ");
        put_dec(&line_text, r.failed);
        text_buffer detail_text = { status2_buf, 0 };
        put_name(&detail_text, r.first_failed->name, 40);
        put_str(&detail_text, " ");
        put_hex(&detail_text, (u32)r.first_error);
        m->status2 = status2_buf;
    }
    if (r.applied) {
        int res = mods_drop_index();
        if (res < 0)
            saltysd_status.last_fs_result = res;
        restart(m);
    }
}

static const char *const net_stage_names[] = {
    "ok", "no Wi-Fi", "http:C", "context", "request", "status", "too big", "receive", "aborted",
};

static void put_net_stage(text_buffer *line_text, const net_result *net)
{
    put_str(line_text, net->stage < sizeof(net_stage_names) / sizeof(net_stage_names[0]) ?
               net_stage_names[net->stage] : "?");
}

static void put_net_detail(text_buffer *line_text, const net_result *net)
{
    if (net->http_status) {
        put_str(line_text, "HTTP ");
        put_dec(line_text, net->http_status);
    } else {
        put_hex(line_text, (u32)net->result);
    }
}

static void check_updates(menu *m, u32 channel)
{
    set_status(m, "Checking for updates...", 0);
    draw(m);

    update_check *r = &last_check;
    update_check_run(r, channel, m->code);
    m->view = VIEW_MAIN;

    text_buffer line_text = { status_buf, 0 };
    text_buffer detail_text = { status2_buf, 0 };
    switch (r->outcome) {
    case UPDATE_CURRENT:
        put_str(&line_text, "Up to date");
        put_str(&detail_text, "Server's build: ");
        put_str(&detail_text, r->identity);
        break;
    case UPDATE_AVAILABLE:
        if (channel == CHANNEL_DIRTY)
            put_str(&line_text, "Dirty build available: ");
        else if (SALTYSD_IS_DIRTY)
            put_str(&line_text, "Leave dirty: install ");
        else
            put_str(&line_text, "Update available: ");
        put_str(&line_text, r->identity);
        put_str(&detail_text, r->file);
        put_str(&detail_text, " (");
        put_dec(&detail_text, r->file_size);
        put_str(&detail_text, " bytes)");
        m->view = VIEW_OFFER;
        break;
    case UPDATE_NET_FAILED:
        if (r->net.stage == NET_NO_WIFI) {
            put_str(&line_text, "No Wi-Fi connection");
            break;
        }
        put_str(&line_text, r->net_file ? "manifest.sig: " : "manifest.txt: ");
        put_net_stage(&line_text, &r->net);
        put_net_detail(&detail_text, &r->net);
        break;
    case UPDATE_CODE_REJECTED:
        put_str(&line_text, "Invalid code");
        break;
    case UPDATE_BAD_FORMAT:
        put_str(&line_text, "Manifest not understood");
        break;
    case UPDATE_UNKNOWN_KEY:
        put_str(&line_text, "Manifest key ");
        put_dec(&line_text, r->key_id);
        put_str(&line_text, " is unknown");
        break;
    case UPDATE_KEY_NOT_ALLOWED:
        put_str(&line_text, "Key ");
        put_dec(&line_text, r->key_id);
        put_str(&line_text, " may not sign this channel");
        break;
    case UPDATE_WRONG_CHANNEL:
        put_str(&line_text, "Invalid channel");
        break;
    case UPDATE_BAD_SIGNATURE:
        put_str(&line_text, "Invalid update. Aborting install.");
        break;
    default:
        put_str(&line_text, "No update for your region");
        break;
    }
    set_status(m, status_buf, detail_text.len ? status2_buf : 0);
}

typedef struct {
    menu *m;
    u32 shown;
} progress_state;

static void show_progress(void *ctx, u32 done, u32 total)
{
    progress_state *p = ctx;
    if (done != total && done - p->shown < PROGRESS_STEP)
        return;
    p->shown = done;

    text_buffer line_text = { status_buf, 0 };
    put_str(&line_text, "Downloading ");
    put_dec(&line_text, done);
    put_str(&line_text, " / ");
    put_dec(&line_text, total);
    set_status(p->m, status_buf, 0);
    draw(p->m);
}

static const char *const install_stage_names[] = {
    "done", "plugin path", "plugin path shape", "SD access", "create .new", "download", "write",
    "size", "hash", "read-back", "backup", "swap (old file restored)", "swap (NOT restored)",
};

static void install_update(menu *m)
{
    progress_state p = { m, 0 };
    update_install_result r;
    m->view = VIEW_BUSY;
    if (update_install(&last_check, &r, show_progress, &p)) {
        restart(m);
        m->view = VIEW_MAIN;
        return;
    }

    m->view = VIEW_MAIN;
    text_buffer line_text = { status_buf, 0 };
    text_buffer detail_text = { status2_buf, 0 };
    put_str(&line_text, "Install failed: ");
    put_str(&line_text, r.stage < sizeof(install_stage_names) / sizeof(install_stage_names[0]) ?
                install_stage_names[r.stage] : "?");
    if (r.stage == INSTALL_DOWNLOAD) {
        put_net_stage(&detail_text, &r.net);
        put_str(&detail_text, " ");
        put_net_detail(&detail_text, &r.net);
    } else {
        put_hex(&detail_text, (u32)r.result);
    }
    set_status(m, status_buf, status2_buf);
}

#if SALTYSD_IS_DIRTY
static void run_gate(menu *m)
{
    if (update_gate(&last_check) != GATE_EXPIRED)
        return;
    saltysd_status.gate_expired++;

    set_status(m, "Dirty access expired. Installing stable...", 0);
    m->view = VIEW_BUSY;
    draw(m);
    update_check_run(&last_check, CHANNEL_STABLE, 0);
    m->view = VIEW_MAIN;
    if (last_check.outcome != UPDATE_AVAILABLE) {
        set_status(m, "Dirty access expired; stable not reachable", 0);
        return;
    }
    install_update(m);
}
#endif

static void move_mod_cursor(menu *m, int down)
{
    if (!mods_count)
        return;
    if (down)
        m->mod_cursor = m->mod_cursor + 1 == mods_count ? 0 : m->mod_cursor + 1;
    else
        m->mod_cursor = m->mod_cursor ? m->mod_cursor - 1 : mods_count - 1;

    if (m->mod_cursor < m->mod_first)
        m->mod_first = m->mod_cursor;
    else if (m->mod_cursor >= m->mod_first + MOD_ROWS)
        m->mod_first = m->mod_cursor - MOD_ROWS + 1;
}

static u32 press_main(menu *m, u32 pressed)
{
    if (pressed & KEY_B)
        return EXIT_BACK;
    if (pressed & KEY_UP)
        m->cursor = m->cursor ? m->cursor - 1 : ITEM_COUNT - 1;
    if (pressed & KEY_DOWN)
        m->cursor = (m->cursor + 1) % ITEM_COUNT;
    if (!(pressed & KEY_A))
        return EXIT_NONE;

    switch (m->cursor) {
    case ITEM_MODS:
        open_mods(m);
        return EXIT_NONE;
    case ITEM_REBUILD:
        m->view = VIEW_REBUILD;
        set_status(m, 0, 0);
        return EXIT_NONE;
    case ITEM_STABLE:
        check_updates(m, CHANNEL_STABLE);
        return EXIT_NONE;
    case ITEM_DIRTY:
        update_load_code(m->code);
        m->code_pos = 0;
        m->view = VIEW_CODE;
        set_status(m, 0, 0);
        return EXIT_NONE;
    default:
        return EXIT_BACK;
    }
}

static u32 press_mods(menu *m, u32 pressed)
{
    if (pressed & KEY_B) {
        fs_close();
        m->view = VIEW_MAIN;
        set_status(m, 0, 0);
        return EXIT_NONE;
    }
    if (pressed & KEY_UP)
        move_mod_cursor(m, 0);
    if (pressed & KEY_DOWN)
        move_mod_cursor(m, 1);
    if ((pressed & KEY_A) && mods_count) {
        mods[m->mod_cursor].wanted ^= 1;
        set_status(m, 0, 0);
    }
    if (pressed & KEY_START) {
        if (mods_changes())
            m->view = VIEW_CONFIRM;
        else
            set_status(m, "Nothing to apply: no mods changed.", 0);
    }
    return EXIT_NONE;
}

static u32 press_confirm(menu *m, u32 pressed)
{
    if (pressed & KEY_A) {
        m->view = VIEW_MODS;
        apply_mods(m);
    } else if (pressed & KEY_B) {
        m->view = VIEW_MODS;
    }
    return EXIT_NONE;
}

//A failed restart leaves the busy view up; any press returns to the menu,
//keeping the error on screen.
static u32 press_busy(menu *m, u32 pressed)
{
    (void)pressed;
    m->view = VIEW_MAIN;
    return EXIT_NONE;
}

static u32 press_code(menu *m, u32 pressed)
{
    char *d = &m->code[m->code_pos];
    if (pressed & KEY_UP)
        *d = *d == '9' ? '0' : *d + 1;
    if (pressed & KEY_DOWN)
        *d = *d == '0' ? '9' : *d - 1;
    if (pressed & KEY_LEFT)
        m->code_pos = m->code_pos ? m->code_pos - 1 : 3;
    if (pressed & KEY_RIGHT)
        m->code_pos = (m->code_pos + 1) % 4;
    if (pressed & KEY_B) {
        m->view = VIEW_MAIN;
        set_status(m, 0, 0);
    } else if (pressed & KEY_A) {
        check_updates(m, CHANNEL_DIRTY);
    }
    return EXIT_NONE;
}

static u32 press_offer(menu *m, u32 pressed)
{
    if (pressed & KEY_A)
        m->view = VIEW_INSTALL_CONFIRM;
    else if (pressed & KEY_B)
        m->view = VIEW_MAIN;
    return EXIT_NONE;
}

static u32 press_install_confirm(menu *m, u32 pressed)
{
    if (pressed & KEY_A)
        install_update(m);
    else if (pressed & KEY_B)
        m->view = VIEW_OFFER;
    return EXIT_NONE;
}

static void hint_rebuild(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "Rebuild cache and restart?", TEXT_RGB);
    draw_text(fb, s, 8, 20, "The next boot might take several minutes", DIM_RGB);
    draw_text(fb, s, 8, 32, "A: Yes  B: No", CURSOR_RGB);
}

static void rebuild_index(menu *m)
{
    int res = fs_open();
    if (res < 0) {
        fs_failed(m, "SD access", res);
        return;
    }

    res = mods_drop_index();
    if (res < 0) {
        fs_close();
        fs_failed(m, "Removing the index", res);
        return;
    }

    restart(m);
}

static u32 press_rebuild(menu *m, u32 pressed)
{
    if (pressed & KEY_A) {
        m->view = VIEW_MAIN;
        rebuild_index(m);
    } else if (pressed & KEY_B) {
        m->view = VIEW_MAIN;
    }
    return EXIT_NONE;
}

typedef struct {
    void (*top)(u8 *fb, const screen *s, const menu *m);
    void (*bottom)(u8 *fb, const screen *s, const menu *m);
    u32  (*press)(menu *m, u32 pressed);
} view_def;

static const view_def views[VIEW_COUNT] = {
    [VIEW_MAIN]            = { paint_main, hint_main,            press_main },
    [VIEW_MODS]            = { paint_mods, hint_mods,            press_mods },
    [VIEW_CONFIRM]         = { paint_mods, hint_confirm,         press_confirm },
    [VIEW_BUSY]            = { paint_busy, 0,                    press_busy },
    [VIEW_CODE]            = { paint_code, hint_code,            press_code },
    [VIEW_OFFER]           = { paint_main, hint_offer,           press_offer },
    [VIEW_INSTALL_CONFIRM] = { paint_main, hint_install_confirm, press_install_confirm },
    [VIEW_REBUILD]         = { paint_main, hint_rebuild,         press_rebuild },
};

static void paint_top(u8 *fb, const screen *s, const menu *m)
{
    clear(fb, s, BG_RGB);
    views[m->view].top(fb, s, m);
}

static void paint_bottom(u8 *fb, const screen *s, const menu *m)
{
    clear(fb, s, BG_RGB);
    if (views[m->view].bottom)
        views[m->view].bottom(fb, s, m);
    if (m->status)
        draw_text(fb, s, 8, 68, m->status, TEXT_RGB);
    if (m->status2)
        draw_text(fb, s, 8, 80, m->status2, TEXT_RGB);
}

static int draw(const menu *m)
{
    display_frame top, bottom;
    if (!display_begin(&m->output, &top, &bottom))
        return 0;

    if (m->output.has_top) {
        paint_top(top.left, &top.layout, m);
        if (top.right)
            paint_top(top.right, &top.layout, m);
    }
    if (m->output.has_bottom)
        paint_bottom(bottom.left, &bottom.layout, m);
    return display_present(&m->output, &top, &bottom);
}

static u32 run_loop(menu *m)
{
    u32 held = input_held();
    u32 settle = SETTLE_FRAMES;
    int dirty = 1;

    for (;;) {
        u32 pending = applet_pending();
        if (pending)
            return pending;

        u32 now = input_held();
        u32 pressed = now & ~held;
        held = now;

        if (m->view >= VIEW_COUNT)
            m->view = VIEW_MAIN;

        if (pressed) {
            u32 reason = views[m->view].press(m, pressed);
            if (reason)
                return reason;
            dirty = 1;
        }

        //Fill both game buffers during the opening frames. Each draw is queued
        //for presentation instead of racing the currently displayed buffer.
        if (dirty || settle) {
            if (draw(m)) {
                dirty = 0;
                if (settle)
                    settle--;
            }
        }
        sleep_ns(POLL_NS);
    }
}

static void make_title(void)
{
    text_buffer line_text = { title_buf, 0 };
    put_str(&line_text, "SaltySD ");
    put_str(&line_text, SALTYSD_IDENTITY);
#ifdef SALTYSD_UPDATE_TEST
    put_str(&line_text, "  TEST BUILD");
#endif
}

void host_menu_run(void)
{
    if (!input_open())
        return;

    menu m;
    m.view = VIEW_MAIN;
    m.cursor = ITEM_MODS;
    m.mod_cursor = m.mod_first = 0;
    m.code[0] = m.code[1] = m.code[2] = m.code[3] = '0';
    m.code_pos = 0;
    m.status = m.status2 = 0;
    if (!display_open(&m.output))
        return;
    make_title();

    display_frame top, bottom;
    if (display_begin(&m.output, &top, &bottom)) {
        saltysd_status.menu_top_fb = m.output.has_top ? (u32)top.left : 0;
        saltysd_status.menu_top_fb_right = m.output.has_top ? (u32)top.right : 0;
        saltysd_status.menu_bottom_fb = m.output.has_bottom ? (u32)bottom.left : 0;
        saltysd_status.menu_formats =
            (m.output.has_top ? top.layout.format : 0xFF) << 8 |
            (m.output.has_bottom ? bottom.layout.format : 0xFF);
    }

#if SALTYSD_IS_DIRTY
    run_gate(&m);
#endif

    u32 reason = run_loop(&m);
    fs_close();
    saltysd_status.menu_exit_reason = reason;
    if (reason != EXIT_BACK)
        return;

    //A button still held here would reach the main menu.
    while (input_held() & (KEY_A | KEY_B))
        sleep_ns(POLL_NS);
}
