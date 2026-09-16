#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(log_task, LOG_LEVEL_DBG);

#define STACK_SIZE       2048
#define SENSOR_COUNT       20
#define SENSOR_PERIOD_MS  100

#define LOGGER_Q_DEPTH 8

/* ================================================================== */
/*  Shared channel message                                            */
/* ================================================================== */

struct sensor_data {
    int16_t illuminance_lx; /* in lux */
    uint32_t timestamp_ms;
    uint8_t seq;
};

/* Forward declarations required before observer/channel definitions. */
static void display_listener_cb(const struct zbus_channel *chan);
static void on_logger_wdt(int channel_id, void *user_data);

static int logger_wdt_id;

/* ================================================================== */
/*  Observers                                                         */
/* ================================================================== */

ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);

/* Logger subscriber that receives messages from the sensor thread */
ZBUS_SUBSCRIBER_DEFINE(logger_sub, LOGGER_Q_DEPTH);



/* ================================================================== */
/*  Channel                                                           */
/* ================================================================== */

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
                 NULL, NULL,
				 ZBUS_OBSERVERS(display_lis, logger_sub),
                 ZBUS_MSG_INIT(.illuminance_lx = 0,
                               .timestamp_ms = 0,
                               .seq = 0));

/* ================================================================== */
/*  Listener - synchronous observer                                   */
/* ================================================================== */

static void display_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *msg =
        (const struct sensor_data *)zbus_chan_const_msg(chan);

    /*
     * Listener runs in publisher context.
     */
    LOG_INF("[DISPLAY-LIS] thread=%s seq=%u light=%d lx",
            k_thread_name_get(k_current_get()),
            msg->seq,
            msg->illuminance_lx);
}

/* ================================================================== */
/*  Publisher                                                         */
/* ================================================================== */

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "sensor");

    for (int i = 0; i < SENSOR_COUNT; i++) {
        struct sensor_data data = {
            .illuminance_lx = 100 + (i * 80),
            .timestamp_ms = k_uptime_get_32(),
            .seq = (uint8_t)i,
        };

        LOG_INF("[SENSOR] publish seq=%u light=%d lx",
                data.seq,
                data.illuminance_lx);

        int ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
        if (ret != 0) {
            LOG_WRN("[SENSOR] publish failed ret=%d", ret);
        }

        k_msleep(SENSOR_PERIOD_MS);
    }

    LOG_INF("[SENSOR] done");
}

/* ================================================================== */
/*  Logger thread that receives messages from the sensor thread       */
/* ================================================================== */

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
    logger_wdt_id = task_wdt_add(2000, /* timeout ms */
        on_logger_wdt, /* called if thread misses feed */
        (void *)k_current_get());
    
    LOG_INF("[LOGGER-MSG] wdt registered with id=%d", logger_wdt_id);

    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "logger");

    const struct zbus_channel *chan;
    int received = 0;

    while (received < SENSOR_COUNT) {
        struct sensor_data msg;

        int ret = zbus_sub_wait(&logger_sub, &chan, K_MSEC(1500));
        if (ret != 0) {
            LOG_WRN("[LOGGER] timeout ret=%d", ret);
            break;
        }
        ret = zbus_chan_read(chan, &msg, K_MSEC(100));
        if (ret != 0) {
            LOG_WRN("[LOGGER] read failed ret=%d", ret);
            continue;
        }

        received++;

        LOG_INF("[LOGGER-MSG] thread=%s seq=%u light=%d latency=%ums",
                k_thread_name_get(k_current_get()),
                msg.seq,
                msg.illuminance_lx,
                k_uptime_get_32() - msg.timestamp_ms);

        if (received == 5) {
            LOG_WRN("[LOGGER-MSG] consumer stuck");
            k_msleep(5000);  /* > timeout, sin feed */
        }

        task_wdt_feed(logger_wdt_id);

        /*
         * Slow logger.
         */
        k_msleep(300);
    }

    LOG_INF("[LOGGER-MSG] done received=%d", received);
    task_wdt_delete(logger_wdt_id);
}

/* ================================================================== */
/*  Health check thread that monitors queue fill level and logs a warning at 75% capacity    */
/* ================================================================== */

static void health_check_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "health-check");

    while (1) {
        k_msleep(1000);
        int used = k_msgq_num_used_get(logger_sub.queue);
        int pct  = (used * 100) / LOGGER_Q_DEPTH;
        if (pct >= 75) {
            LOG_WRN("[HEALTH] queue fill=%d/%d (%d%%)", used, LOGGER_Q_DEPTH, pct);
        }
    }
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
                NULL, NULL, NULL, 6, 0, 100);

K_THREAD_DEFINE(health_check_thread, STACK_SIZE, health_check_thread_fn,
                NULL, NULL, NULL, 7, 0, 100);

/* ================================================================== */
/*  Callback for watchdog missed feed                                    */
/* ================================================================== */

static void on_logger_wdt(int channel_id, void *user_data)
{
    ARG_UNUSED(channel_id);
    ARG_UNUSED(user_data);
    LOG_WRN("[LOGGER-WDT] missed feed / consumer stuck");
}
/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    task_wdt_init(NULL); /* NULL = software-only; pass hw wdt device for fallback */
   
    LOG_INF("=== L5 Task1: Memory, Resource Constraints and Reliability===");
    LOG_INF("sensor publishes every %dms", SENSOR_PERIOD_MS);
    LOG_INF("display listener runs in publisher context");
    LOG_INF("logger receives messages from the sensor thread");

    return 0;
}
