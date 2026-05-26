/**
 * threads.cpp
 * Implementations of SharedState lifecycle and all five thread functions:
 *   waiter_thread, chef_thread, monitor_thread, manager_thread, canceller_thread
 *
 * Locking discipline (must never be violated to prevent deadlocks):
 *   1. Only ONE of {stats_mutex, print_mutex} may be held at a time.
 *   2. queue::mutex_ is never held while acquiring stats_mutex or print_mutex.
 *   3. kitchen_sem is a POSIX semaphore – no ordering constraint with mutexes.
 *   4. log_msg() acquires print_mutex: never call it while holding stats_mutex.
 */
#include "threads.h"
#include "order.h"
#include <cstdlib>     // rand_r
#include <ctime>       // time, clock_gettime
#include <cerrno>
#include <unistd.h>    // usleep

// ─────────────────────────── SharedState ──────────────────────────────────────

SharedState::SharedState()
    : running(false),
      next_order_id(1),
      target_chefs(MIN_ACTIVE_CHEFS),
      active_cooking(0),
      total_completed(0),
      total_cancelled(0),
      total_wait_ms(0.0)
{
    // Initialise kitchen semaphore to MAX_CONCURRENT_CHEFS.
    // pshared=0 → semaphore is shared between threads of the same process only.
    sem_init(&kitchen_sem, /*pshared=*/0, MAX_CONCURRENT_CHEFS);
    pthread_mutex_init(&stats_mutex, nullptr);
    pthread_mutex_init(&print_mutex, nullptr);
    start_time = std::chrono::steady_clock::now();
}

SharedState::~SharedState() {
    sem_destroy(&kitchen_sem);
    pthread_mutex_destroy(&stats_mutex);
    pthread_mutex_destroy(&print_mutex);
}

// ─────────────────────────── Utility: log_msg ─────────────────────────────────

void log_msg(SharedState* state, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    pthread_mutex_lock(&state->print_mutex);    // ← acquire print lock
    vprintf(fmt, args);
    fflush(stdout);
    pthread_mutex_unlock(&state->print_mutex);  // ← release print lock
    va_end(args);
}

// ─────────────────────────── Waiter Thread ────────────────────────────────────

/**
 * waiter_thread  (Producer)
 *
 * Each Waiter independently generates customer orders and pushes them onto
 * the shared OrderQueue.  The order ID is obtained atomically via fetch_add
 * so that concurrent Waiters never produce duplicate IDs.
 *
 * Inter-arrival time: uniform random in [600 ms, 1 600 ms], giving a combined
 * arrival rate of ~3 × (1/1.1 s) ≈ 2.7 orders / sec across all Waiters.
 */
void* waiter_thread(void* arg) {
    WaiterArg*   wa    = static_cast<WaiterArg*>(arg);
    SharedState* state = wa->state;
    const int    id    = wa->id;

    // Per-thread seed: avoids lock contention on the global rand() state and
    // ensures each Waiter generates an independent sequence.
    unsigned int seed = static_cast<unsigned int>(time(nullptr))
                        ^ static_cast<unsigned int>(id * 0x9E3779B9u);

    while (state->running.load()) {

        // ── Build a new order ───────────────────────────────────────────────
        int      oid     = state->next_order_id.fetch_add(1);   // atomic: no race
        int      prep_ms = 500 + static_cast<int>(rand_r(&seed) % 2501); // 500–3 000 ms
        // Approximately 25 % of orders are VIP
        Priority prio    = (rand_r(&seed) % 4 == 0) ? Priority::VIP : Priority::NORMAL;

        Order order(oid, prep_ms, prio);

        // ── Enqueue (thread-safe via OrderQueue::mutex_) ────────────────────
        state->queue.enqueue(order);

        log_msg(state,
                "[Waiter %d] ► New Order  #%-4d  priority=%-6s  prep=%4d ms\n",
                id, oid, priority_str(prio), prep_ms);

        // ── Random inter-arrival delay ──────────────────────────────────────
        int delay_us = 600'000 + static_cast<int>(rand_r(&seed) % 1'000'001);
        usleep(static_cast<useconds_t>(delay_us));
    }

    log_msg(state, "[Waiter %d] Shutting down.\n", id);
    return nullptr;
}

// ─────────────────────────── Chef Thread ──────────────────────────────────────

/**
 * chef_thread  (Consumer)
 *
 * Lifecycle per iteration:
 *   1. [Dynamic check]  If chef_id > target_chefs: sleep 300 ms and retry.
 *      This is how the Manager thread effectively "parks" excess Chefs.
 *
 *   2. [Dequeue]  Block on OrderQueue::dequeue() until an order is available.
 *      The dequeue call internally uses pthread_cond_timedwait (200 ms timeout)
 *      so it remains responsive to shutdown.
 *
 *   3. [Kitchen semaphore]  sem_timedwait(&kitchen_sem) acquires one of the
 *      MAX_CONCURRENT_CHEFS kitchen slots.  Uses a 1-second timeout loop so
 *      that shutdown can be detected even while all slots are occupied.
 *
 *   4. [Cook]  usleep(prep_time_ms * 1000) simulates food preparation.
 *      active_cooking is incremented before and decremented after this sleep.
 *
 *   5. [Release]  sem_post(&kitchen_sem) frees the kitchen slot for another Chef.
 *
 *   6. [Stats]  total_completed and total_wait_ms are updated under stats_mutex.
 */
void* chef_thread(void* arg) {
    ChefArg*     ca    = static_cast<ChefArg*>(arg);
    SharedState* state = ca->state;
    const int    id    = ca->id;

    while (state->running.load()) {

        // ── Step 1: Dynamic allocation check ────────────────────────────────
        // The Manager thread adjusts target_chefs in [MIN_ACTIVE_CHEFS, MAX_ACTIVE_CHEFS].
        // Chefs with id > target_chefs are in "standby": they sleep briefly
        // and keep polling until they are needed again.
        if (id > state->target_chefs.load()) {
            usleep(300'000);  // 300 ms standby poll interval
            continue;
        }

        // ── Step 2: Dequeue an order (blocking) ─────────────────────────────
        // dequeue() sleeps on not_empty_ condvar (200 ms timeout).
        // Returns false when running == false → break out of the main loop.
        Order order;
        if (!state->queue.dequeue(order, state->running)) {
            break;  // shutdown signal received while waiting for work
        }

        // ── Step 3: Acquire a kitchen slot via semaphore ─────────────────────
        // sem_timedwait decrements kitchen_sem.  If it is already 0
        // (MAX_CONCURRENT_CHEFS chefs are currently cooking), this Chef blocks.
        // We use a 1-second timeout loop so that shutdown can preempt the wait.
        bool got_slot = false;
        while (state->running.load()) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;  // try for up to 1 second at a time

            if (sem_timedwait(&state->kitchen_sem, &ts) == 0) {
                got_slot = true;   // successfully decremented the semaphore
                break;
            }
            // errno == ETIMEDOUT → loop and check running again
        }
        if (!got_slot) {
            // Shutdown occurred while waiting for a kitchen slot;
            // order is silently dropped – the simulation is ending.
            break;
        }

        // ── Step 4: Cook the order ────────────────────────────────────────────
        {
            // Record how long this order waited in the queue.
            auto   now     = std::chrono::steady_clock::now();
            double wait_ms = std::chrono::duration<double, std::milli>(
                                 now - order.enqueue_time).count();

            state->active_cooking.fetch_add(1);

            log_msg(state,
                    "[Chef  %d] ✔ Picked     Order #%-4d  (%s)\n",
                    id, order.id, priority_str(order.priority));
            log_msg(state,
                    "[Chef  %d] ⚙ Processing Order #%-4d  (prep: %4d ms)\n",
                    id, order.id, order.prep_time_ms);

            // Simulate food preparation
            usleep(static_cast<useconds_t>(order.prep_time_ms) * 1'000U);

            log_msg(state,
                    "[Chef  %d] ✓ Completed  Order #%-4d  (waited: %.0f ms)\n",
                    id, order.id, wait_ms);

            // ── Step 6: Update performance metrics (under stats_mutex) ────────
            // stats_mutex is acquired AFTER releasing all other per-operation
            // state to keep the critical section minimal.
            pthread_mutex_lock(&state->stats_mutex);   // ← acquire stats lock
            state->total_completed++;
            state->total_wait_ms += wait_ms;
            pthread_mutex_unlock(&state->stats_mutex); // ← release stats lock

            state->active_cooking.fetch_sub(1);
        }

        // ── Step 5: Release kitchen slot ─────────────────────────────────────
        // sem_post increments kitchen_sem, potentially unblocking another Chef
        // that is blocked in Step 3.
        sem_post(&state->kitchen_sem);
    }

    log_msg(state, "[Chef  %d] Shutting down.\n", id);
    return nullptr;
}

// ─────────────────────────── Monitor Thread ───────────────────────────────────

/**
 * monitor_thread
 *
 * Prints a continuously-updating live dashboard to stdout every 2 seconds.
 * All reads from shared state use atomic loads or stat_mutex-protected copies,
 * so the snapshot printed is always self-consistent within each field.
 */
void* monitor_thread(void* arg) {
    SharedState* state = static_cast<SharedState*>(arg);

    while (state->running.load()) {
        usleep(2'000'000);           // 2-second refresh interval
        if (!state->running.load()) break;

        // ── Collect metrics ────────────────────────────────────────────────
        int    q_size  = state->queue.size();          // internally locked
        int    cooking = state->active_cooking.load(); // atomic
        int    target  = state->target_chefs.load();   // atomic

        pthread_mutex_lock(&state->stats_mutex);       // ← acquire stats lock
        int    completed  = state->total_completed;
        int    cancelled  = state->total_cancelled;
        double total_wait = state->total_wait_ms;
        pthread_mutex_unlock(&state->stats_mutex);     // ← release stats lock

        double avg_wait_ms = (completed > 0) ? (total_wait / completed) : 0.0;

        auto   now_tp  = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
                             now_tp - state->start_time).count();
        double tput    = (elapsed > 0.0) ? (completed / elapsed) : 0.0;

        // ── Render dashboard (print_mutex serialises with other log_msg calls) ─
        pthread_mutex_lock(&state->print_mutex);       // ← acquire print lock
        printf("\n");
        printf("╔════════════════════════════════════════════════════╗\n");
        printf("║     RESTAURANT MANAGEMENT  ─  LIVE DASHBOARD       ║\n");
        printf("╠════════════════════════════════════════════════════╣\n");
        printf("║  Queue size          : %-5d                        ║\n", q_size);
        printf("║  Chefs cooking/target: %d / %d                        ║\n", cooking, target);
        printf("║  Kitchen capacity    : %d slots max (semaphore)      ║\n", MAX_CONCURRENT_CHEFS);
        printf("╠════════════════════════════════════════════════════╣\n");
        printf("║  Orders completed    : %-5d                        ║\n", completed);
        printf("║  Orders cancelled    : %-5d                        ║\n", cancelled);
        printf("║  Avg queue wait time : %7.1f ms                   ║\n", avg_wait_ms);
        printf("║  Throughput          : %6.2f orders / sec          ║\n", tput);
        printf("║  Uptime              : %6.1f sec                   ║\n", elapsed);
        printf("╚════════════════════════════════════════════════════╝\n");
        printf("\n");
        fflush(stdout);
        pthread_mutex_unlock(&state->print_mutex);     // ← release print lock
    }
    return nullptr;
}

// ─────────────────────────── Manager Thread ───────────────────────────────────

/**
 * manager_thread  (Dynamic Chef Allocation)
 *
 * Wakes every 3 seconds and compares the current queue depth against two
 * thresholds:
 *
 *   queue ≥ QUEUE_HIGH_THRESHOLD → scale UP   (target_chefs++)
 *   queue ≤ QUEUE_LOW_THRESHOLD  → scale DOWN  (target_chefs--)
 *   otherwise                    → no change
 *
 * target_chefs is clamped to [MIN_ACTIVE_CHEFS, MAX_ACTIVE_CHEFS].
 * The atomic fetch_add / fetch_sub ensure that concurrent reads by Chef threads
 * always see a consistent value.
 *
 * Increasing target_chefs causes standby Chefs (see Step 1 of chef_thread) to
 * wake from their 300 ms sleep and start processing orders.  Decreasing it
 * causes excess Chefs to return to standby on their next iteration.
 */
void* manager_thread(void* arg) {
    SharedState* state = static_cast<SharedState*>(arg);

    while (state->running.load()) {
        usleep(3'000'000);           // evaluation interval: 3 s
        if (!state->running.load()) break;

        int q    = state->queue.size();
        int curr = state->target_chefs.load();

        if (q >= QUEUE_HIGH_THRESHOLD && curr < MAX_ACTIVE_CHEFS) {
            // Queue is backing up → activate one more Chef
            int next = state->target_chefs.fetch_add(1) + 1;
            log_msg(state,
                    "[Manager] ▲ Queue high (%d orders)  → target chefs: %d → %d\n",
                    q, curr, next);

        } else if (q <= QUEUE_LOW_THRESHOLD && curr > MIN_ACTIVE_CHEFS) {
            // Queue is draining → put one Chef into standby
            int next = state->target_chefs.fetch_sub(1) - 1;
            log_msg(state,
                    "[Manager] ▼ Queue low  (%d orders)  → target chefs: %d → %d\n",
                    q, curr, next);
        }
    }
    return nullptr;
}

// ─────────────────────────── Canceller Thread ─────────────────────────────────

/**
 * canceller_thread  (Random Order Cancellation)
 *
 * Every 4–7 seconds, selects a random pending order from a snapshot of the
 * queue and attempts to cancel it via OrderQueue::cancel_order().
 *
 * Thread-safety analysis:
 *   - snapshot()     → acquires queue::mutex_, returns a deep copy, releases.
 *   - cancel_order() → acquires queue::mutex_, marks order.cancelled, releases.
 *   If the target order was already dequeued between snapshot() and
 *   cancel_order(), cancel_order() returns false; this is harmless – the
 *   cancellation is simply a no-op.
 *   If cancel_order() returns true, the order will be skipped by the next
 *   cleanup_cancelled() pass inside dequeue(), and the counter is incremented
 *   under stats_mutex.
 */
void* canceller_thread(void* arg) {
    SharedState*  state = static_cast<SharedState*>(arg);
    unsigned int  seed  = static_cast<unsigned int>(time(nullptr)) ^ 0xDEADC0DEu;

    while (state->running.load()) {
        // Random wait between 4 and 7 seconds
        usleep(4'000'000 + static_cast<int>(rand_r(&seed) % 3'000'001));
        if (!state->running.load()) break;

        // ── Snapshot: get a copy of the current queue ──────────────────────
        std::vector<Order> snap = state->queue.snapshot();

        // Collect IDs of non-cancelled orders (snapshot may contain stale entries)
        std::vector<int> candidates;
        candidates.reserve(snap.size());
        for (const Order& o : snap) {
            if (!o.cancelled) candidates.push_back(o.id);
        }
        if (candidates.empty()) continue;   // nothing to cancel right now

        // ── Pick a random candidate ────────────────────────────────────────
        int chosen_id = candidates[rand_r(&seed) % candidates.size()];

        // ── Thread-safe cancellation ───────────────────────────────────────
        // cancel_order locks queue::mutex_ internally; no lock needed here.
        if (state->queue.cancel_order(chosen_id)) {
            // Update cancelled counter under stats_mutex
            pthread_mutex_lock(&state->stats_mutex);   // ← acquire stats lock
            state->total_cancelled++;
            pthread_mutex_unlock(&state->stats_mutex); // ← release stats lock

            log_msg(state, "[Canceller] ✗ Cancelled Order #%d\n", chosen_id);
        }
        // If cancel_order() returned false: order already dequeued – safe to ignore.
    }
    return nullptr;
}
