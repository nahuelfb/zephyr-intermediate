#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_sync, LOG_LEVEL_INF);

#define THREAD_STACK  1024
#define WORKER_PRIO   5
#define STEPS_EACH    1000000

static volatile uint32_t shared_count;
static struct k_sem workers_done;
static K_MUTEX_DEFINE(shared_lock);

static void inc_shared(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const char *who = k_thread_name_get(k_current_get());

	for (int n = 0; n < STEPS_EACH; n++) {
		k_mutex_lock(&shared_lock, K_FOREVER);
		shared_count++;
		k_mutex_unlock(&shared_lock);
	}

	LOG_INF("thread %s done", who);
	k_sem_give(&workers_done);
}

K_THREAD_DEFINE(inc_a, THREAD_STACK, inc_shared, NULL, NULL, NULL,
		WORKER_PRIO, 0, 0);
K_THREAD_DEFINE(inc_b, THREAD_STACK, inc_shared, NULL, NULL, NULL,
		WORKER_PRIO, 0, 0);

int main(void)
{
	const uint32_t expected = STEPS_EACH * 2U;
	int64_t t0 = k_uptime_get();

	k_sem_init(&workers_done, 0, 2);

	LOG_INF("L2 task1: two workers, shared counter, mutex");
	LOG_INF("expect %u", expected);

	k_sem_take(&workers_done, K_FOREVER);
	k_sem_take(&workers_done, K_FOREVER);

	LOG_INF("got %u", shared_count);

	if (shared_count == expected) {
		LOG_WRN("count matches (no lost updates this run)");
	} else {
		LOG_ERR("lost %u updates", expected - shared_count);
	}

	LOG_INF("elapsed %lld ms", k_uptime_delta(&t0));
	return 0;
}