/**
 * threads.h
 * Shared state, configuration constants, and thread-entry-point declarations
 * for the Restaurant Order Management Simulator.
 *
 * ── Threading Model ────────────────────────────────────────────────────────────
 *
 *  Thread role      | Count | Role
 *  -----------------|-------|---------------------------------------------
 *  Waiter           | 3     | Producer – generates and enqueues orders
 *  Chef             | 5     | Consumer – dequeues, acquires kitchen slot, cooks
 *  Monitor          | 1     | Prints live dashboard every 2 s
 *  Manager          | 1     | Dynamically adjusts target_chefs
 *  Canceller        | 1     | Randomly cancels pending orders
 *
 * ── Synchronisation Primitives ────────────────────────────────────────────────
 *
 *  Primitive                   | Purpose
 *  ----------------------------|------------------------------------------------
 *  queue::mutex_               | Guards the order vector inside OrderQueue
 *  queue::not_empty_           | Condition: signals Chefs when orders arrive
 *  sem_t kitchen_sem           | Counting semaphore – limits simultaneous cooking
 *  pthread_mutex_t stats_mutex | Guards performance counters (completed, wait ms)
 *  pthread_mutex_t print_mutex | Serialises all console output across threads
 */
#pragma once

#include <pthread.h>
#include <semaphore.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include "queue.h"

// ──────────────────────────── Configuration ──────────────────────────────────

static constexpr int NUM_WAITERS          = 3;   ///< Producer threads
static constexpr int NUM_CHEFS            = 5;   ///< Consumer threads (total pool)
static constexpr int MAX_CONCURRENT_CHEFS = 4;   ///< Kitchen semaphore initial value
static constexpr int MIN_ACTIVE_CHEFS     = 2;   ///< Minimum target_chefs (dynamic floor)
static constexpr int MAX_ACTIVE_CHEFS     = NUM_CHEFS; ///< Dynamic ceiling
static constexpr int SIM_DURATION_SECS    = 60;  ///< Auto-shutdown after N seconds

/// Queue-depth thresholds used by the Manager thread
static constexpr int QUEUE_HIGH_THRESHOLD = 8;   ///< Scale up  when queue ≥ this
static constexpr int QUEUE_LOW_THRESHOLD  = 2;   ///< Scale down when queue ≤ this

// ────────────────────────────── SharedState ───────────────────────────────────

/**
 * Central shared state passed by pointer to every thread function.
 *
 * Ownership / access rules:
 *   queue           → all reads/writes go through OrderQueue's own mutex
 *   kitchen_sem     → POSIX semaphore (wait/post; no additional mutex needed)
 *   running         → std::atomic (lock-free)
 *   next_order_id   → std::atomic (lock-free fetch_add)
 *   target_chefs    → std::atomic (lock-free; read by Chef, written by Manager)
 *   active_cooking  → std::atomic (lock-free; incremented/decremented by Chef)
 *   total_completed,
 *   total_cancelled,
 *   total_wait_ms   → guarded by stats_mutex
 *   All printf calls → serialised by print_mutex via log_msg()
 */
struct SharedState {

    // ── Order queue ─────────────────────────────────────────────────────────
    OrderQueue queue;

    // ── Kitchen semaphore ────────────────────────────────────────────────────
    /**
     * Counting semaphore initialised to MAX_CONCURRENT_CHEFS.
     *
     * sem_wait(&kitchen_sem):
     *   Called by a Chef BEFORE starting to cook.  Decrements the count.
     *   Blocks if count == 0 (all kitchen slots occupied).
     *
     * sem_post(&kitchen_sem):
     *   Called by a Chef AFTER completing cooking.  Increments the count,
     *   potentially unblocking another Chef that is waiting for a slot.
     *
     * This is the mechanism that limits simultaneous cooking to at most
     * MAX_CONCURRENT_CHEFS, regardless of how many Chef threads are active.
     */
    sem_t kitchen_sem;

    // ── Lifecycle ────────────────────────────────────────────────────────────
    std::atomic<bool> running;         ///< false → all threads should exit
    std::atomic<int>  next_order_id;   ///< Monotonically increasing; fetch_add(1)

    // ── Dynamic chef control ─────────────────────────────────────────────────
    /**
     * target_chefs:
     *   The Manager thread writes this value.
     *   Each Chef thread reads it; if (chef_id > target_chefs) the Chef sleeps
     *   and retries, effectively putting that slot in standby.
     *   Range: [MIN_ACTIVE_CHEFS, MAX_ACTIVE_CHEFS].
     */
    std::atomic<int>  target_chefs;

    /**
     * active_cooking:
     *   Incremented by a Chef just before usleep(prep_time), decremented after.
     *   Displayed on the monitoring dashboard.
     *   Not used for any synchronisation decision – purely informational.
     */
    std::atomic<int>  active_cooking;

    // ── Performance metrics ──────────────────────────────────────────────────
    pthread_mutex_t stats_mutex;       ///< Guards the three counters below
    int    total_completed;            ///< Orders successfully cooked
    int    total_cancelled;            ///< Orders cancelled before cooking
    double total_wait_ms;              ///< Cumulative queue-wait time across all orders

    std::chrono::steady_clock::time_point start_time; ///< Simulator wall-clock start

    // ── Console output serialisation ─────────────────────────────────────────
    /**
     * print_mutex:
     *   All calls to printf/vprintf are wrapped by log_msg(), which acquires
     *   this mutex.  This prevents garbled interleaved output when multiple
     *   threads print simultaneously.
     */
    pthread_mutex_t print_mutex;

    SharedState();
    ~SharedState();
};

// ──────────────────────────── Thread Arguments ───────────────────────────────

struct WaiterArg { int id; SharedState* state; };
struct ChefArg   { int id; SharedState* state; };

// ────────────────────────── Thread Entry Points ──────────────────────────────

void* waiter_thread   (void* arg);   ///< Producer: generates orders
void* chef_thread     (void* arg);   ///< Consumer: cooks orders
void* monitor_thread  (void* arg);   ///< Prints live dashboard
void* manager_thread  (void* arg);   ///< Adjusts target_chefs dynamically
void* canceller_thread(void* arg);   ///< Randomly cancels pending orders

// ────────────────────────────── Utilities ────────────────────────────────────

/**
 * Thread-safe printf wrapper.
 * Acquires state->print_mutex, calls vprintf, flushes stdout, then releases.
 * All thread functions MUST use this instead of bare printf to avoid
 * interleaved / garbled output.
 */
void log_msg(SharedState* state, const char* fmt, ...);
