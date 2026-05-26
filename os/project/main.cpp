/**
 * main.cpp
 * Entry point for the Restaurant Order Management Simulator.
 *
 * Compile:
 *   g++ -std=c++14 -Wall -Wextra -O2 -pthread \
 *       main.cpp order.cpp queue.cpp threads.cpp \
 *       -o restaurant_sim
 *
 *   Note: On older Linux (glibc < 2.17) add -lrt for POSIX semaphores.
 *
 * Run:
 *   ./restaurant_sim
 *   Press Ctrl+C to stop early (graceful shutdown is handled).
 */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <unistd.h>    // write(), STDOUT_FILENO
#include <pthread.h>
#include "threads.h"
#include "order.h"

// ── Global pointer used only by the async signal handler ──────────────────────
static SharedState* g_state = nullptr;

/**
 * SIGINT / SIGTERM handler.
 * Signals all threads to stop by setting running=false and broadcasting on
 * the queue's condition variable.
 *
 * Only async-signal-safe calls (write, atomic store) are used here.
 * printf is NOT async-signal-safe and must not be called from a signal handler.
 */
static void signal_handler(int /*sig*/) {
    if (g_state) {
        static const char msg[] =
            "\n[System] Signal caught – initiating graceful shutdown...\n";
        ssize_t n = write(STDOUT_FILENO, msg, sizeof(msg) - 1); // async-signal-safe
        (void)n;
        g_state->running.store(false);
        g_state->queue.shutdown();  // wake threads blocked in dequeue()
    }
}

// ─────────────────────────────── main ─────────────────────────────────────────

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    // ── Print banner ───────────────────────────────────────────────────────────
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║       Restaurant Order Management Simulator  v1.0        ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Waiter threads  : %-3d  (producers)                      ║\n", NUM_WAITERS);
    printf("║  Chef threads    : %-3d  (consumers; start active: %d)     ║\n",
           NUM_CHEFS, MIN_ACTIVE_CHEFS);
    printf("║  Kitchen slots   : %-3d  (max concurrent cooks, semaphore) ║\n",
           MAX_CONCURRENT_CHEFS);
    printf("║  Duration        : %-3d s (Ctrl+C to stop early)           ║\n",
           SIM_DURATION_SECS);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    fflush(stdout);

    // ── Initialise shared state ────────────────────────────────────────────────
    SharedState state;
    g_state = &state;

    // ── Install signal handlers ────────────────────────────────────────────────
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // ── Start simulation ───────────────────────────────────────────────────────
    state.running.store(true);
    state.start_time = std::chrono::steady_clock::now();

    // ── Thread handles and argument structs ────────────────────────────────────
    pthread_t waiter_tids  [NUM_WAITERS];
    pthread_t chef_tids    [NUM_CHEFS];
    pthread_t monitor_tid, manager_tid, canceller_tid;

    WaiterArg waiter_args[NUM_WAITERS];
    ChefArg   chef_args  [NUM_CHEFS];

    // ── Launch Waiter (producer) threads ──────────────────────────────────────
    for (int i = 0; i < NUM_WAITERS; ++i) {
        waiter_args[i] = {i + 1, &state};
        if (pthread_create(&waiter_tids[i], nullptr,
                           waiter_thread, &waiter_args[i]) != 0) {
            perror("pthread_create waiter");
            return 1;
        }
        printf("[System] Waiter %d started.\n", i + 1);
    }

    // ── Launch Chef (consumer) threads ────────────────────────────────────────
    for (int i = 0; i < NUM_CHEFS; ++i) {
        chef_args[i] = {i + 1, &state};
        if (pthread_create(&chef_tids[i], nullptr,
                           chef_thread, &chef_args[i]) != 0) {
            perror("pthread_create chef");
            return 1;
        }
        const char* status = (i + 1 <= MIN_ACTIVE_CHEFS) ? "active" : "standby";
        printf("[System] Chef   %d started (%s).\n", i + 1, status);
    }

    // ── Launch utility threads ─────────────────────────────────────────────────
    if (pthread_create(&monitor_tid,   nullptr, monitor_thread,   &state) != 0 ||
        pthread_create(&manager_tid,   nullptr, manager_thread,   &state) != 0 ||
        pthread_create(&canceller_tid, nullptr, canceller_thread, &state) != 0) {
        perror("pthread_create utility");
        return 1;
    }
    printf("[System] Monitor, Manager, and Canceller threads started.\n\n");
    fflush(stdout);

    // ── Run until time expires or Ctrl+C ──────────────────────────────────────
    for (int t = 0; t < SIM_DURATION_SECS && state.running.load(); ++t) {
        sleep(1);
    }

    // ── Initiate graceful shutdown ─────────────────────────────────────────────
    if (state.running.load()) {          // normal expiry (not Ctrl+C)
        printf("\n[System] Simulation time elapsed. Shutting down...\n");
        fflush(stdout);
        state.running.store(false);
        state.queue.shutdown();          // broadcast to wake dequeue() waiters
    }

    // ── Join all threads ───────────────────────────────────────────────────────
    // Waiter threads check 'running' each iteration; they will exit promptly.
    for (int i = 0; i < NUM_WAITERS; ++i)
        pthread_join(waiter_tids[i], nullptr);

    // Chef threads may still be cooking (usleep). We wait for them to finish
    // their current order naturally before they check 'running' and exit.
    for (int i = 0; i < NUM_CHEFS; ++i)
        pthread_join(chef_tids[i], nullptr);

    pthread_join(monitor_tid,   nullptr);
    pthread_join(manager_tid,   nullptr);
    pthread_join(canceller_tid, nullptr);

    // ── Final statistics report ────────────────────────────────────────────────
    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - state.start_time).count();

    double tput     = (elapsed > 0.0)
                      ? static_cast<double>(state.total_completed) / elapsed
                      : 0.0;
    double avg_wait = (state.total_completed > 0)
                      ? state.total_wait_ms / state.total_completed
                      : 0.0;

    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                 FINAL SIMULATION REPORT                   ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Total orders completed : %-5d                           ║\n",
           state.total_completed);
    printf("║  Total orders cancelled : %-5d                           ║\n",
           state.total_cancelled);
    printf("║  Average queue wait     : %7.1f ms                      ║\n",
           avg_wait);
    printf("║  Throughput             : %6.2f orders / sec            ║\n",
           tput);
    printf("║  Total runtime          : %6.1f sec                     ║\n",
           elapsed);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    return 0;
}
