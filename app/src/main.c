#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_sync, LOG_LEVEL_INF);

#define THREAD_STACK  1024
#define WORKER_PRIO   5
#define STEPS_EACH    1000000

static volatile uint32_t shared_count;
static struct k_sem workers_done;

#if IS_ENABLED(CONFIG_APP_USE_MUTEX)
static K_MUTEX_DEFINE(shared_lock);
#endif

/*
 * Read-modify-write of shared_count.
 *
 * Unprotected: load into a local, yield (preemption window), then store.
 * Two equal-priority threads can both read the same value and both write
 * value+1, so one increment is lost.
 *
 * Protected: the mutex makes the whole update exclusive, so the total is
 * always STEPS_EACH * 2.
 */
static void inc_shared(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const char *who = k_thread_name_get(k_current_get());

	for (int n = 0; n < STEPS_EACH; n++) {
#if IS_ENABLED(CONFIG_APP_USE_MUTEX)
		k_mutex_lock(&shared_lock, K_FOREVER);
		shared_count++;
		k_mutex_unlock(&shared_lock);
#else
		uint32_t tmp = shared_count;

		/* Open the race window between load and store. */
		k_yield();
		shared_count = tmp + 1U;
#endif
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

#if IS_ENABLED(CONFIG_APP_USE_MUTEX)
	LOG_INF("L2 task1: PROTECTED (mutex) shared counter");
#else
	LOG_INF("L2 task1: UNPROTECTED shared counter (race demo)");
#endif
	LOG_INF("expect %u", expected);

	k_sem_take(&workers_done, K_FOREVER);
	k_sem_take(&workers_done, K_FOREVER);

	LOG_INF("got %u", shared_count);

	if (shared_count == expected) {
		LOG_WRN("count matches expected (no lost updates this run)");
	} else {
		LOG_ERR("lost %u updates (got %u, expected %u)",
			expected - shared_count, shared_count, expected);
	}

	LOG_INF("elapsed %lld ms", k_uptime_delta(&t0));

	/*
	 * Why the protected result is always correct:
	 * the mutex serializes the read-modify-write, so only one worker
	 * updates shared_count at a time. Unprotected runs can interleave
	 * load/store and drop increments; protected runs cannot.
	 */
	return 0;
}
