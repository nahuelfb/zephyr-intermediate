#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_HIGH    3
#define PRIO_MEDIUM  5
#define PRIO_LOW     7
#define PRIO_COOP   (-1)

void t_high_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[HIGH] started");

    for (int i = 0; i < 10; i++) {
        LOG_INF("[HIGH] step %d  tick=%u", i, k_uptime_get_32());
        k_msleep(100);
    }

    LOG_INF("[HIGH] done");
}

void t_medium_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[MEDIUM] started");

    for (int i = 0; i < 10; i++) {
        LOG_INF("[MEDIUM] step %d  tick=%u", i, k_uptime_get_32());
        k_msleep(200);
    }
}

void t_low_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[LOW] started");

    for (int i = 0; i < 10; i++) {
        LOG_INF("[LOW] step %d  tick=%u", i, k_uptime_get_32());

        k_msleep(300);
    }

    LOG_INF("[LOW] done");
}

void t_coop_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[COOP] starting - 5 busy steps, no yield yet");
    for (int i = 0; i < 5; i++) {
        k_busy_wait(40000);  /* quema CPU; no es sleep */
        LOG_INF("[COOP] busy %d/5  tick=%u", i + 1, k_uptime_get_32());
    }
    LOG_INF("[COOP] yielding");
    k_yield();
    LOG_INF("[COOP] done");
}

K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn, NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_medium, STACK_SIZE, t_medium_fn, NULL, NULL, NULL, PRIO_MEDIUM, 0, 0);
K_THREAD_DEFINE(t_low,  STACK_SIZE, t_low_fn,  NULL, NULL, NULL, PRIO_LOW,  0, 0);
K_THREAD_DEFINE(t_coop, STACK_SIZE, t_coop_fn, NULL, NULL, NULL, PRIO_COOP, 0, 0);

int main(void)
{
    LOG_INF("=== L1 Task 1: Scheduling Competition ===");
    LOG_INF("HIGH prio=%d  MEDIUM prio=%d  LOW prio=%d",
            PRIO_HIGH, PRIO_MEDIUM, PRIO_LOW);
    return 0;
}

