# L6 Task 1 — Latency investigation write-up

## Response-time guarantee

The guarantee measured here is **start latency**, not full processing time:

> From timer expiry (`ready_ms` stamped in the ISR) until the **control**
> thread begins handling the event (`k_uptime_get_32()` right after
> `k_msgq_get` returns), latency must be **≤ 10 ms**.

Detector: `latency_ms = now - event.ready_ms`. Every miss increments
`deadline_misses` and is logged with `LOG_WRN_RATELIMIT`.

## Evidence summary

| Run | Priorities | Typical `latency_ms` | Deadline misses |
|-----|------------|----------------------|-----------------|
| Before | control **7**, maintenance **4** | **40 ms** | count rises every sequence |
| After | control **4**, maintenance **7** | **0 ms** | none |

Console captures: `01-before-priorities-console.log`,
`02-after-correction-console.log`.

## Kernel shell (`kernel thread list`)

See `05-kernel-shell-thread-list.txt`.

Between events:
- **maintenance** — priority **4**, state **pending** (on the semaphore)
- **control** — priority **7**, state **pending** (on the message queue)

In Zephyr, a **lower number is higher priority**, so maintenance outranks
control. Stack usage was ~1 %; the failure is not a stack overflow.

## CTF / Babeltrace — delayed sequence (before)

Excerpt: `03-before-babeltrace-delayed-seq.txt` (decoded with Babeltrace2 from
a POSIX CTF capture on `native_sim`).

For `seq=0`:

1. `named_event event_ready` — timer made both threads ready
2. `thread_switched_in: maintenance` — higher priority wins
3. `thread_busy_wait_enter` for **45000 µs** — control stays ready but does not run
4. `thread_switched_in: control` only after busy-wait ends
5. `named_event event_done` with **arg1 = 40** — start latency 40 ms (> 10 ms)

So control was not stuck pending without work: it was **ready and delayed** by
a higher-priority runnable thread (scheduler delay).

## CTF / Babeltrace — after correction

Excerpt: `04-after-babeltrace-on-time-seq.txt`.

1. `event_ready`
2. `thread_switched_in: control` **first**
3. `event_done` with **arg1 = 0**
4. Then maintenance runs its busy-wait

Same workload and detector; only priorities changed.

## Root cause

On each timer tick the ISR posts to the control queue **and** gives the
maintenance semaphore. Both threads become ready. With maintenance at priority
4 and a **45 ms** `k_busy_wait`, control (priority 7) cannot run until that
busy work finishes → ~40 ms start latency and systematic deadline misses.

## Correction

Swap priorities so **control is higher** (4) than **maintenance** (7).

Justification: control has a short hard deadline (10 ms); maintenance is
important background work without that deadline. Raising control priority
removes the scheduling delay without changing the functional work of either
thread. Re-measurement with the same detector shows `latency_ms=0` and no
deadline-miss growth.
