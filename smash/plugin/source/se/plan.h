#ifndef SALTYSD_SE_PLAN_H
#define SALTYSD_SE_PLAN_H

#include "types.h"
#include "se/errors.h"

#define SE_PLAN_PACKAGES_MAX 62
#define SE_PLAN_RELATIONS_MAX 256
#define SE_PLAN_CLAIMS_MAX 256
#define SE_PACKAGE_ID_CHARS 64
#define SE_PLAN_NONE 0xFFFFu

typedef struct {
    u16 major;
    u16 minor;
    u16 patch;
} se_version;

typedef enum {
    SE_PACKAGE_DISCOVERED = 0,
    SE_PACKAGE_DISABLED = 1,
    SE_PACKAGE_PLANNED = 2,
    SE_PACKAGE_APPLIED = 3,
    SE_PACKAGE_ACTIVE = 4,
    SE_PACKAGE_PENDING = 5,
    SE_PACKAGE_REFUSED = 6,
} se_package_state;

typedef struct {
    char id[SE_PACKAGE_ID_CHARS];
    se_version version;
    u16 discovery_index;
    u16 state;
    u16 error;
    u16 peer;
    u16 order;
} se_plan_package;

typedef struct {
    u16 owner;
    u16 optional;
    char target[SE_PACKAGE_ID_CHARS];
    se_version minimum;
    se_version maximum;
} se_plan_dependency;

typedef struct {
    u16 owner;
    u16 after;
    char target[SE_PACKAGE_ID_CHARS];
} se_plan_order_constraint;

typedef struct {
    u16 owner;
    char target[SE_PACKAGE_ID_CHARS];
} se_plan_conflict;

typedef struct {
    u16 owner;
    u32 start;
    u32 end;
} se_plan_claim;

typedef struct {
    se_plan_package packages[SE_PLAN_PACKAGES_MAX];
    se_plan_dependency dependencies[SE_PLAN_RELATIONS_MAX];
    se_plan_order_constraint constraints[SE_PLAN_RELATIONS_MAX];
    se_plan_conflict conflicts[SE_PLAN_RELATIONS_MAX];
    se_plan_claim claims[SE_PLAN_CLAIMS_MAX];
    u16 order[SE_PLAN_PACKAGES_MAX];
    u16 package_count;
    u16 dependency_count;
    u16 constraint_count;
    u16 conflict_count;
    u16 claim_count;
    u16 order_count;
} se_plan;

void se_plan_init(se_plan *plan);
int se_plan_valid_id(const char *id);
int se_plan_add_package(se_plan *plan, const char *id, se_version version,
                        u16 discovery_index, int enabled);
int se_plan_add_dependency(se_plan *plan, u16 owner, const char *target,
                           se_version minimum, se_version maximum, int optional);
int se_plan_add_conflict(se_plan *plan, u16 owner, const char *target);
int se_plan_add_before(se_plan *plan, u16 owner, const char *target);
int se_plan_add_after(se_plan *plan, u16 owner, const char *target);
int se_plan_add_claim(se_plan *plan, u16 owner, u32 start, u32 end);
void se_plan_refuse(se_plan *plan, u16 package, se_error error, u16 peer);
void se_plan_propagate_refusals(se_plan *plan);
void se_plan_run(se_plan *plan);

#endif
