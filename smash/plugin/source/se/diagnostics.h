#ifndef SALTYSD_SE_DIAGNOSTICS_H
#define SALTYSD_SE_DIAGNOSTICS_H

#include "se/plan.h"

#define SE_DIAGNOSTIC_EVENTS_MAX 128
#define SE_DISPLAY_NAME_CHARS 48

typedef enum {
    SE_PHASE_NONE = 0,
    SE_PHASE_DISCOVERY = 1,
    SE_PHASE_PARSING = 2,
    SE_PHASE_BUILD = 3,
    SE_PHASE_DEPENDENCY = 4,
    SE_PHASE_SIGNATURE = 5,
    SE_PHASE_CLAIM = 6,
    SE_PHASE_COMMIT = 7,
    SE_PHASE_INIT = 8,
    SE_PHASE_CRO = 9,
    SE_PHASE_HOOK = 10,
    SE_PHASE_RUNTIME = 11,
} se_diagnostic_phase;

typedef struct {
    char id[SE_PACKAGE_ID_CHARS];
    char display_name[SE_DISPLAY_NAME_CHARS];
    u16 state;
    u16 phase;
    u16 error;
    u16 peer;
    u32 detail_a;
    u32 detail_b;
} se_package_diagnostic;

typedef struct {
    u16 package;
    u16 phase;
    u16 error;
    u16 reserved;
    u32 detail_a;
    u32 detail_b;
} se_diagnostic_event;

void se_diagnostics_reset(void);
void se_diagnostics_package(u16 package, const char *id, const char *display_name,
                            se_package_state state);
void se_diagnostics_state(u16 package, se_package_state state);
void se_diagnostics_emit(u16 package, se_diagnostic_phase phase, se_error error,
                         u16 peer, u32 detail_a, u32 detail_b);
const se_package_diagnostic *se_diagnostics_packages(u32 *count);
const se_diagnostic_event *se_diagnostics_events(u32 *count, u32 *dropped);

#endif
