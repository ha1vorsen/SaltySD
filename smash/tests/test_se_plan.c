#include <assert.h>
#include <string.h>

#include "se/plan.h"

static const se_version V1 = { 1, 0, 0 };
static const se_version V2 = { 2, 0, 0 };
static const se_version ANY = { 0, 0, 0 };

static void deterministic_order(void)
{
    se_plan plan;
    se_plan_init(&plan);
    int c = se_plan_add_package(&plan, "org.test.c", V1, 0, 1);
    int b = se_plan_add_package(&plan, "org.test.b", V1, 1, 1);
    int a = se_plan_add_package(&plan, "org.test.a", V1, 2, 1);
    assert(a >= 0 && b >= 0 && c >= 0);
    assert(se_plan_add_dependency(&plan, (u16)b, "org.test.a", V1, V1, 0));
    assert(se_plan_add_after(&plan, (u16)c, "org.test.b"));
    se_plan_run(&plan);
    assert(plan.order_count == 3);
    assert(strcmp(plan.packages[plan.order[0]].id, "org.test.a") == 0);
    assert(strcmp(plan.packages[plan.order[1]].id, "org.test.b") == 0);
    assert(strcmp(plan.packages[plan.order[2]].id, "org.test.c") == 0);
}

static void collisions_refuse_all_owners(void)
{
    se_plan plan;
    se_plan_init(&plan);
    int a = se_plan_add_package(&plan, "org.test.a", V1, 0, 1);
    int b = se_plan_add_package(&plan, "org.test.b", V1, 1, 1);
    int c = se_plan_add_package(&plan, "org.test.c", V1, 2, 1);
    assert(se_plan_add_claim(&plan, (u16)a, 0x1000, 0x1020));
    assert(se_plan_add_claim(&plan, (u16)b, 0x1010, 0x1030));
    assert(se_plan_add_claim(&plan, (u16)c, 0x2000, 0x2010));
    se_plan_run(&plan);
    assert(plan.packages[a].error == SE_ERROR_STATIC_COLLISION);
    assert(plan.packages[b].error == SE_ERROR_STATIC_COLLISION);
    assert(plan.packages[c].state == SE_PACKAGE_PLANNED);
    assert(plan.order_count == 1 && plan.order[0] == c);
}

static void dependency_failures_propagate(void)
{
    se_plan plan;
    se_plan_init(&plan);
    int a = se_plan_add_package(&plan, "org.test.a", V1, 0, 1);
    int b = se_plan_add_package(&plan, "org.test.b", V1, 1, 1);
    int c = se_plan_add_package(&plan, "org.test.c", V1, 2, 1);
    assert(se_plan_add_dependency(&plan, (u16)a, "org.test.missing", ANY, ANY, 0));
    assert(se_plan_add_dependency(&plan, (u16)b, "org.test.a", ANY, ANY, 0));
    assert(se_plan_add_dependency(&plan, (u16)c, "org.test.b", ANY, ANY, 0));
    se_plan_run(&plan);
    assert(plan.packages[a].error == SE_ERROR_MISSING_DEPENDENCY);
    assert(plan.packages[b].error == SE_ERROR_DEPENDENCY_REFUSED);
    assert(plan.packages[c].error == SE_ERROR_DEPENDENCY_REFUSED);
}

static void versions_duplicates_and_cycles(void)
{
    se_plan plan;
    se_plan_init(&plan);
    int a = se_plan_add_package(&plan, "org.test.a", V1, 0, 1);
    int b = se_plan_add_package(&plan, "org.test.b", V1, 1, 1);
    int v = se_plan_add_package(&plan, "org.test.v", V1, 2, 1);
    int user = se_plan_add_package(&plan, "org.test.user", V1, 3, 1);
    int d1 = se_plan_add_package(&plan, "org.test.duplicate", V1, 4, 1);
    int d2 = se_plan_add_package(&plan, "org.test.duplicate", V1, 5, 1);
    assert(se_plan_add_dependency(&plan, (u16)a, "org.test.b", ANY, ANY, 0));
    assert(se_plan_add_dependency(&plan, (u16)b, "org.test.a", ANY, ANY, 0));
    assert(se_plan_add_dependency(&plan, (u16)user, "org.test.v", V2, ANY, 0));
    se_plan_run(&plan);
    assert(plan.packages[a].error == SE_ERROR_DEPENDENCY_CYCLE);
    assert(plan.packages[b].error == SE_ERROR_DEPENDENCY_CYCLE);
    assert(plan.packages[v].state == SE_PACKAGE_PLANNED);
    assert(plan.packages[user].error == SE_ERROR_DEPENDENCY_VERSION);
    assert(plan.packages[d1].error == SE_ERROR_DUPLICATE_ID);
    assert(plan.packages[d2].error == SE_ERROR_DUPLICATE_ID);
}

int main(void)
{
    assert(se_plan_valid_id("org.saltysd.example"));
    assert(!se_plan_valid_id("Example"));
    assert(!se_plan_valid_id("missingdot"));
    deterministic_order();
    collisions_refuse_all_owners();
    dependency_failures_propagate();
    versions_duplicates_and_cycles();
    return 0;
}
