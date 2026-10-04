#include "diagnostics.h"

static se_package_diagnostic packages[SE_PLAN_PACKAGES_MAX];
static se_diagnostic_event events[SE_DIAGNOSTIC_EVENTS_MAX];
static u32 package_count;
static u32 event_count;
static u32 dropped_events;

static void copy_text(char *out, u32 capacity, const char *text)
{
    u32 at = 0;
    if (text)
        while (text[at] && at + 1 < capacity) {
            out[at] = text[at];
            at++;
        }
    out[at] = 0;
}

void se_diagnostics_reset(void)
{
    package_count = 0;
    event_count = 0;
    dropped_events = 0;
}

void se_diagnostics_package(u16 package, const char *id, const char *display_name,
                            se_package_state state)
{
    if (package >= SE_PLAN_PACKAGES_MAX)
        return;
    if (package_count <= package)
        package_count = package + 1;
    se_package_diagnostic *record = &packages[package];
    copy_text(record->id, sizeof(record->id), id);
    copy_text(record->display_name, sizeof(record->display_name), display_name);
    record->state = state;
    record->phase = SE_PHASE_NONE;
    record->error = PLUGIN_OK;
    record->peer = SE_PLAN_NONE;
    record->detail_a = 0;
    record->detail_b = 0;
}

void se_diagnostics_state(u16 package, se_package_state state)
{
    if (package < package_count)
        packages[package].state = state;
}

void se_diagnostics_emit(u16 package, se_diagnostic_phase phase, se_error error,
                         u16 peer, u32 detail_a, u32 detail_b)
{
    if (package < package_count) {
        se_package_diagnostic *record = &packages[package];
        record->phase = phase;
        record->error = error;
        record->peer = peer;
        record->detail_a = detail_a;
        record->detail_b = detail_b;
        if (error != PLUGIN_OK)
            record->state = SE_PACKAGE_REFUSED;
    }
    if (event_count == SE_DIAGNOSTIC_EVENTS_MAX) {
        dropped_events++;
        return;
    }
    se_diagnostic_event *event = &events[event_count++];
    event->package = package;
    event->phase = phase;
    event->error = error;
    event->reserved = 0;
    event->detail_a = detail_a;
    event->detail_b = detail_b;
}

const se_package_diagnostic *se_diagnostics_packages(u32 *count)
{
    *count = package_count;
    return packages;
}

const se_diagnostic_event *se_diagnostics_events(u32 *count, u32 *dropped)
{
    *count = event_count;
    *dropped = dropped_events;
    return events;
}
