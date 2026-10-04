#include "plan.h"

static int text_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int text_less(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (u8)*a < (u8)*b;
}

static int copy_id(char out[SE_PACKAGE_ID_CHARS], const char *id)
{
    u32 at = 0;
    while (id[at]) {
        if (at == SE_PACKAGE_ID_CHARS - 1)
            return 0;
        out[at] = id[at];
        at++;
    }
    out[at] = 0;
    return 1;
}

static int version_compare(se_version a, se_version b)
{
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    return 0;
}

static int version_zero(se_version value)
{
    return !value.major && !value.minor && !value.patch;
}

static int version_in_range(se_version value, se_version minimum, se_version maximum)
{
    if (version_compare(value, minimum) < 0)
        return 0;
    return version_zero(maximum) || version_compare(value, maximum) <= 0;
}

static int find_package(const se_plan *plan, const char *id)
{
    for (u32 i = 0; i < plan->package_count; i++)
        if (text_equal(plan->packages[i].id, id))
            return (int)i;
    return -1;
}

static void refuse(se_plan *plan, u16 package, se_error error, u16 peer)
{
    se_plan_package *item = &plan->packages[package];
    if (item->state == SE_PACKAGE_REFUSED || item->state == SE_PACKAGE_DISABLED)
        return;
    item->state = SE_PACKAGE_REFUSED;
    item->error = (u16)error;
    item->peer = peer;
    item->order = SE_PLAN_NONE;
}

void se_plan_refuse(se_plan *plan, u16 package, se_error error, u16 peer)
{
    if (package < plan->package_count)
        refuse(plan, package, error, peer);
}

void se_plan_init(se_plan *plan)
{
    plan->package_count = 0;
    plan->dependency_count = 0;
    plan->constraint_count = 0;
    plan->conflict_count = 0;
    plan->claim_count = 0;
    plan->order_count = 0;
}

int se_plan_valid_id(const char *id)
{
    u32 length = 0;
    int dot = 0;
    int label_start = 1;
    while (id[length]) {
        u8 c = (u8)id[length];
        if (length == SE_PACKAGE_ID_CHARS - 1)
            return 0;
        if (c == '.') {
            if (label_start || !length || id[length - 1] == '-')
                return 0;
            dot = 1;
            label_start = 1;
        } else {
            int alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (!alnum && c != '-')
                return 0;
            if (label_start && !alnum)
                return 0;
            label_start = 0;
        }
        length++;
    }
    return length && dot && !label_start && id[length - 1] != '-';
}

int se_plan_add_package(se_plan *plan, const char *id, se_version version,
                        u16 discovery_index, int enabled)
{
    if (plan->package_count == SE_PLAN_PACKAGES_MAX)
        return -1;
    u16 index = plan->package_count++;
    se_plan_package *package = &plan->packages[index];
    package->id[0] = 0;
    package->version = version;
    package->discovery_index = discovery_index;
    package->state = enabled ? SE_PACKAGE_DISCOVERED : SE_PACKAGE_DISABLED;
    package->error = PLUGIN_OK;
    package->peer = SE_PLAN_NONE;
    package->order = SE_PLAN_NONE;
    if (!copy_id(package->id, id) || !se_plan_valid_id(package->id))
        refuse(plan, index, SE_ERROR_INVALID_ID, SE_PLAN_NONE);
    return index;
}

int se_plan_add_dependency(se_plan *plan, u16 owner, const char *target,
                           se_version minimum, se_version maximum, int optional)
{
    if (owner >= plan->package_count || plan->dependency_count == SE_PLAN_RELATIONS_MAX)
        return 0;
    se_plan_dependency *relation = &plan->dependencies[plan->dependency_count];
    if (!copy_id(relation->target, target) || !se_plan_valid_id(relation->target))
        return 0;
    relation->owner = owner;
    relation->optional = optional != 0;
    relation->minimum = minimum;
    relation->maximum = maximum;
    plan->dependency_count++;
    return 1;
}

int se_plan_add_conflict(se_plan *plan, u16 owner, const char *target)
{
    if (owner >= plan->package_count || plan->conflict_count == SE_PLAN_RELATIONS_MAX)
        return 0;
    se_plan_conflict *relation = &plan->conflicts[plan->conflict_count];
    if (!copy_id(relation->target, target) || !se_plan_valid_id(relation->target))
        return 0;
    relation->owner = owner;
    plan->conflict_count++;
    return 1;
}

static int add_constraint(se_plan *plan, u16 owner, const char *target, int after)
{
    if (owner >= plan->package_count || plan->constraint_count == SE_PLAN_RELATIONS_MAX)
        return 0;
    se_plan_order_constraint *relation = &plan->constraints[plan->constraint_count];
    if (!copy_id(relation->target, target) || !se_plan_valid_id(relation->target))
        return 0;
    relation->owner = owner;
    relation->after = after != 0;
    plan->constraint_count++;
    return 1;
}

int se_plan_add_before(se_plan *plan, u16 owner, const char *target)
{
    return add_constraint(plan, owner, target, 0);
}

int se_plan_add_after(se_plan *plan, u16 owner, const char *target)
{
    return add_constraint(plan, owner, target, 1);
}

int se_plan_add_claim(se_plan *plan, u16 owner, u32 start, u32 end)
{
    if (owner >= plan->package_count || start >= end || plan->claim_count == SE_PLAN_CLAIMS_MAX)
        return 0;
    se_plan_claim *claim = &plan->claims[plan->claim_count++];
    claim->owner = owner;
    claim->start = start;
    claim->end = end;
    return 1;
}

static int overlaps(const se_plan_claim *a, const se_plan_claim *b)
{
    return a->start < b->end && b->start < a->end;
}

static void reject_duplicate_ids(se_plan *plan)
{
    for (u32 a = 0; a < plan->package_count; a++)
        for (u32 b = a + 1; b < plan->package_count; b++)
            if (text_equal(plan->packages[a].id, plan->packages[b].id)) {
                refuse(plan, a, SE_ERROR_DUPLICATE_ID, (u16)b);
                refuse(plan, b, SE_ERROR_DUPLICATE_ID, (u16)a);
            }
}

static void reject_explicit_conflicts(se_plan *plan)
{
    for (u32 i = 0; i < plan->conflict_count; i++) {
        se_plan_conflict *relation = &plan->conflicts[i];
        int target = find_package(plan, relation->target);
        if (target < 0)
            continue;
        refuse(plan, relation->owner, SE_ERROR_EXPLICIT_CONFLICT, (u16)target);
        refuse(plan, (u16)target, SE_ERROR_EXPLICIT_CONFLICT, relation->owner);
    }
}

static void reject_static_collisions(se_plan *plan)
{
    for (u32 a = 0; a < plan->claim_count; a++) {
        for (u32 b = a + 1; b < plan->claim_count; b++) {
            se_plan_claim *left = &plan->claims[a];
            se_plan_claim *right = &plan->claims[b];
            if (left->owner == right->owner || !overlaps(left, right))
                continue;
            refuse(plan, left->owner, SE_ERROR_STATIC_COLLISION, right->owner);
            refuse(plan, right->owner, SE_ERROR_STATIC_COLLISION, left->owner);
        }
    }
}

static void reject_bad_dependencies(se_plan *plan)
{
    for (u32 i = 0; i < plan->dependency_count; i++) {
        se_plan_dependency *relation = &plan->dependencies[i];
        int target = find_package(plan, relation->target);
        if (target < 0) {
            if (!relation->optional)
                refuse(plan, relation->owner, SE_ERROR_MISSING_DEPENDENCY, SE_PLAN_NONE);
            continue;
        }
        if (!version_in_range(plan->packages[target].version,
                              relation->minimum, relation->maximum))
            refuse(plan, relation->owner, SE_ERROR_DEPENDENCY_VERSION, (u16)target);
    }
}

static void propagate_refusals(se_plan *plan)
{
    int changed;
    do {
        changed = 0;
        for (u32 i = 0; i < plan->dependency_count; i++) {
            se_plan_dependency *relation = &plan->dependencies[i];
            if (relation->optional || plan->packages[relation->owner].state == SE_PACKAGE_REFUSED)
                continue;
            int target = find_package(plan, relation->target);
            if (target >= 0 && plan->packages[target].state == SE_PACKAGE_REFUSED) {
                refuse(plan, relation->owner, SE_ERROR_DEPENDENCY_REFUSED, (u16)target);
                changed = 1;
            }
        }
    } while (changed);
}

void se_plan_propagate_refusals(se_plan *plan)
{
    propagate_refusals(plan);
}

static int predecessor_unselected(const se_plan *plan, u16 candidate, const u8 *selected)
{
    for (u32 i = 0; i < plan->dependency_count; i++) {
        const se_plan_dependency *relation = &plan->dependencies[i];
        if (relation->owner != candidate)
            continue;
        int target = find_package(plan, relation->target);
        if (target >= 0 && plan->packages[target].state != SE_PACKAGE_REFUSED &&
            plan->packages[target].state != SE_PACKAGE_DISABLED && !selected[target])
            return 1;
    }
    for (u32 i = 0; i < plan->constraint_count; i++) {
        const se_plan_order_constraint *relation = &plan->constraints[i];
        int target = find_package(plan, relation->target);
        if (target < 0 || plan->packages[target].state == SE_PACKAGE_REFUSED ||
            plan->packages[target].state == SE_PACKAGE_DISABLED)
            continue;
        u16 predecessor = relation->after ? (u16)target : relation->owner;
        u16 successor = relation->after ? relation->owner : (u16)target;
        if (successor == candidate && !selected[predecessor])
            return 1;
    }
    return 0;
}

static void order_ready_packages(se_plan *plan)
{
    u8 selected[SE_PLAN_PACKAGES_MAX] = { 0 };
    plan->order_count = 0;
    for (;;) {
        int best = -1;
        u32 remaining = 0;
        for (u32 i = 0; i < plan->package_count; i++) {
            se_plan_package *package = &plan->packages[i];
            if (package->state == SE_PACKAGE_REFUSED || package->state == SE_PACKAGE_DISABLED ||
                selected[i])
                continue;
            remaining++;
            if (predecessor_unselected(plan, (u16)i, selected))
                continue;
            if (best < 0 || text_less(package->id, plan->packages[best].id))
                best = (int)i;
        }
        if (!remaining)
            return;
        if (best < 0) {
            for (u32 i = 0; i < plan->package_count; i++)
                if (plan->packages[i].state != SE_PACKAGE_REFUSED &&
                    plan->packages[i].state != SE_PACKAGE_DISABLED && !selected[i])
                    refuse(plan, (u16)i, SE_ERROR_DEPENDENCY_CYCLE, SE_PLAN_NONE);
            return;
        }
        selected[best] = 1;
        plan->packages[best].state = SE_PACKAGE_PLANNED;
        plan->packages[best].order = plan->order_count;
        plan->order[plan->order_count++] = (u16)best;
    }
}

void se_plan_run(se_plan *plan)
{
    plan->order_count = 0;
    for (u32 i = 0; i < plan->package_count; i++) {
        se_plan_package *package = &plan->packages[i];
        if (package->state != SE_PACKAGE_DISABLED && package->error == PLUGIN_OK)
            package->state = SE_PACKAGE_DISCOVERED;
        package->order = SE_PLAN_NONE;
    }
    reject_duplicate_ids(plan);
    reject_explicit_conflicts(plan);
    reject_static_collisions(plan);
    reject_bad_dependencies(plan);
    propagate_refusals(plan);
    order_ready_packages(plan);
    propagate_refusals(plan);
}
