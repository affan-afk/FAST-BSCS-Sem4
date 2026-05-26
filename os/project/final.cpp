/**
 * Restaurant Order Management Simulator
 * Combined Single File Version
 * * Compile:
 * g++ -std=c++14 -Wall -Wextra -O2 -pthread main.cpp -o restaurant_sim
 * * Note: On older Linux (glibc < 2.17) add -lrt for POSIX semaphores.
 * * Run:
 * ./restaurant_sim
 */

// ── STANDARD INCLUDES ────────────────────────────────────────────────────────
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <vector>

// ── CONFIGURATION CONSTANTS ──────────────────────────────────────────────────
static constexpr int NUM_WAITERS          = 3;   ///< Producer threads
static constexpr int NUM_CHEFS            = 5;   ///< Consumer threads (total pool)
static constexpr int MAX_CONCURRENT_CHEFS = 4;   ///< Kitchen semaphore initial value
static constexpr int MIN_ACTIVE_CHEFS     = 2;   ///< Minimum target_chefs (dynamic floor)
static constexpr int MAX_ACTIVE_CHEFS     = NUM_CHEFS; ///< Dynamic ceiling
static constexpr int SIM_DURATION_SECS    = 60;  ///< Auto-shutdown after N seconds

static constexpr int QUEUE_HIGH_THRESHOLD = 8;   ///< Scale up  when queue ≥ this
static constexpr int QUEUE_LOW_THRESHOLD  = 2;   ///< Scale down when queue ≤ this

// ── DATA STRUCTURES: ORDER ───────────────────────────────────────────────────
enum class Priority {
    NORMAL = 0,
    VIP    = 1   ///< VIP orders are always served before NORMAL orders
};

inline const char* priority_str(Priority p) {
    return (p == Priority::VIP) ? "VIP" : "NORMAL";
}

struct Order {
    int      id;            
    int      prep_time_ms;  
    Priority priority;      
    std::chrono::steady_clock::time_point enqueue_time; 
    bool     cancelled;     

    Order();
    Order(int id, int prep_time_ms, Priority priority);
};

// ── DATA STRUCTURES: QUEUE ───────────────────────────────────────────────────
class OrderQueue {
public:
    OrderQueue();
    ~OrderQueue();

    void enqueue(const Order& order);
    bool dequeue(Order& order, const std::atomic<bool>& running);
    bool cancel_order(int order_id);
    int size();
    std::vector<Order> snapshot();
    void shutdown();

private:
    std::vector<Order> orders_;    
    pthread_mutex_t    mutex_;     
    pthread_cond_t     not_empty_; 

    void cleanup_cancelled();
    int find_best() const;
};

// ── DATA STRUCTURES: SHARED STATE ────────────────────────────────────────────
struct SharedState {
    OrderQueue queue;
    sem_t kitchen_sem;

    std::atomic<bool> running;         
    std::atomic<int>  next_order_id;   
    std::atomic<int>  target_chefs;
    std::atomic<int>  active_cooking;

    pthread_mutex_t stats_mutex;       
    int    total_completed;            
    int    total_cancelled;            
    double total_wait_ms;              

    std::chrono::steady_clock::time_point start_time; 

    pthread_mutex_t print_mutex;

    SharedState();
    ~SharedState();
};

struct WaiterArg { int id; SharedState* state; };
struct ChefArg   { int id; SharedState* state; };

// ── THREAD DECLARATIONS & UTILITIES ──────────────────────────────────────────
void* waiter_thread   (void* arg);
void* chef_thread     (void* arg);
void* monitor_thread  (void* arg);
void* manager_thread  (void* arg);
void* canceller_thread(void* arg);
void log_msg(SharedState* state, const char* fmt, ...);

// ── IMPLEMENTATION: ORDER ────────────────────────────────────────────────────
Order::Order()
    : id(0),
      prep_time_ms(0),
      priority(Priority::NORMAL),
      enqueue_time(std::chrono::steady_clock::now()),
      cancelled(false)
{}

Order::Order(int id, int prep_time_ms, Priority priority)
    : id(id),
      prep_time_ms(prep_time_ms),
      priority(priority),
      enqueue_time(std::chrono::steady_clock::now()),
      cancelled(false)
{}

// ── IMPLEMENTATION: QUEUE ────────────────────────────────────────────────────
OrderQueue::OrderQueue() {
    pthread_mutex_init(&mutex_, nullptr);
    pthread_cond_init(&not_empty_, nullptr);
}

OrderQueue::~OrderQueue() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&not_empty_);
}

void OrderQueue::cleanup_cancelled() {
    orders_.erase(
        std::remove_if(orders_.begin(), orders_.end(),
                       [](const Order& o) { return o.cancelled; }),
        orders_.end());
}

int OrderQueue::find_best() const {
    if (orders_.empty()) return -1;

    int best = 0;
    for (int i = 1; i < static_cast<int>(orders_.size()); ++i) {
        const Order& a = orders_[i];
        const Order& b = orders_[best];

        if (static_cast<int>(a.priority) > static_cast<int>(b.priority)) {
            best = i;
            continue;
        }
        if (a.priority == b.priority && a.enqueue_time < b.enqueue_time) {
            best = i;
        }
    }
    return best;
}

void OrderQueue::enqueue(const Order& order) {
    pthread_mutex_lock(&mutex_);         
    orders_.push_back(order);
    pthread_cond_signal(&not_empty_);    
    pthread_mutex_unlock(&mutex_);       
}

bool OrderQueue::dequeue(Order& order, const std::atomic<bool>& running) {
    pthread_mutex_lock(&mutex_);         

    while (running.load()) {
        cleanup_cancelled();

        if (!orders_.empty()) {
            int idx = find_best();       
            order = orders_[idx];
            orders_.erase(orders_.begin() + idx);
            pthread_mutex_unlock(&mutex_);   
            return true;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200'000'000L;     // +200 ms
        if (ts.tv_nsec >= 1'000'000'000L) {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1'000'000'000L;
        }
        pthread_cond_timedwait(&not_empty_, &mutex_, &ts);
    }

    pthread_mutex_unlock(&mutex_);       
    return false;
}

bool OrderQueue::cancel_order(int order_id) {
    pthread_mutex_lock(&mutex_);         
    bool found = false;
    for (Order& o : orders_) {
        if (o.id == order_id && !o.cancelled) {
            o.cancelled = true;          
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_);       
    return found;
}

int OrderQueue::size() {
    pthread_mutex_lock(&mutex_);         
    int count = 0;
    for (const Order& o : orders_) {
        if (!o.cancelled) ++count;
    }
    pthread_mutex_unlock(&mutex_);       
    return count;
}

std::vector<Order> OrderQueue::snapshot() {
    pthread_mutex_lock(&mutex_);         
    std::vector<Order> copy = orders_;  
    pthread_mutex_unlock(&mutex_);       
    return copy;
}

void OrderQueue::shutdown() {
    pthread_mutex_lock(&mutex_);         
    pthread_cond_broadcast(&not_empty_); 
    pthread_mutex_unlock(&mutex_);       
}

// ── IMPLEMENTATION: SHARED STATE & THREADS ───────────────────────────────────
SharedState::SharedState()
    : running(false),
      next_order_id(1),
      target_chefs(MIN_ACTIVE_CHEFS),
      active_cooking(0),
      total_completed(0),
      total_cancelled(0),
      total_wait_ms(0.0)
{
    sem_init(&kitchen_sem, 0, MAX_CONCURRENT_CHEFS);
    pthread_mutex_init(&stats_mutex, nullptr);
    pthread_mutex_init(&print_mutex, nullptr);
    start_time = std::chrono::steady_clock::now();
}

SharedState::~SharedState() {
    sem_destroy(&kitchen_sem);
    pthread_mutex_destroy(&stats_mutex);
    pthread_mutex_destroy(&print_mutex);
}

void log_msg(SharedState* state, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    pthread_mutex_lock(&state->print_mutex);    
    vprintf(fmt, args);
    fflush(stdout);
    pthread_mutex_unlock(&state->print_mutex);  
    va_end(args);
}

void* waiter_thread(void* arg) {
    WaiterArg* wa    = static_cast<WaiterArg*>(arg);
    SharedState* state = wa->state;
    const int    id    = wa->id;

    unsigned int seed = static_cast<unsigned int>(time(nullptr))
                        ^ static_cast<unsigned int>(id * 0x9E3779B9u);

    while (state->running.load()) {
        int      oid     = state->next_order_id.fetch_add(1);   
        int      prep_ms = 500 + static_cast<int>(rand_r(&seed) % 2501); 
        Priority prio    = (rand_r(&seed) % 4 == 0) ? Priority::VIP : Priority::NORMAL;

        Order order(oid, prep_ms, prio);

        state->queue.enqueue(order);

        log_msg(state,
                "[Waiter %d] ► New Order  #%-4d  priority=%-6s  prep=%4d ms\n",
                id, oid, priority_str(prio), prep_ms);

        int delay_us = 600'000 + static_cast<int>(rand_r(&seed) % 1'000'001);
        usleep(static_cast<useconds_t>(delay_us));
    }

    log_msg(state, "[Waiter %d] Shutting down.\n", id);
    return nullptr;
}

void* chef_thread(void* arg) {
    ChefArg* ca    = static_cast<ChefArg*>(arg);
    SharedState* state = ca->state;
    const int    id    = ca->id;

    while (state->running.load()) {
        if (id > state->target_chefs.load()) {
            usleep(300'000);  
            continue;
        }

        Order order;
        if (!state->queue.dequeue(order, state->running)) {
            break;  
        }

        bool got_slot = false;
        while (state->running.load()) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;  

            if (sem_timedwait(&state->kitchen_sem, &ts) == 0) {
                got_slot = true;   
                break;
            }
        }
        if (!got_slot) {
            break;
        }

        {
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

            usleep(static_cast<useconds_t>(order.prep_time_ms) * 1'000U);

            log_msg(state,
                    "[Chef  %d] ✓ Completed  Order #%-4d  (waited: %.0f ms)\n",
                    id, order.id, wait_ms);

            pthread_mutex_lock(&state->stats_mutex);   
            state->total_completed++;
            state->total_wait_ms += wait_ms;
            pthread_mutex_unlock(&state->stats_mutex); 

            state->active_cooking.fetch_sub(1);
        }

        sem_post(&state->kitchen_sem);
    }

    log_msg(state, "[Chef  %d] Shutting down.\n", id);
    return nullptr;
}

void* monitor_thread(void* arg) {
    SharedState* state = static_cast<SharedState*>(arg);

    while (state->running.load()) {
        usleep(2'000'000);           
        if (!state->running.load()) break;

        int    q_size  = state->queue.size();          
        int    cooking = state->active_cooking.load(); 
        int    target  = state->target_chefs.load();   

        pthread_mutex_lock(&state->stats_mutex);       
        int    completed  = state->total_completed;
        int    cancelled  = state->total_cancelled;
        double total_wait = state->total_wait_ms;
        pthread_mutex_unlock(&state->stats_mutex);     

        double avg_wait_ms = (completed > 0) ? (total_wait / completed) : 0.0;

        auto   now_tp  = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
                             now_tp - state->start_time).count();
        double tput    = (elapsed > 0.0) ? (completed / elapsed) : 0.0;

        pthread_mutex_lock(&state->print_mutex);       
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
        pthread_mutex_unlock(&state->print_mutex);     
    }
    return nullptr;
}

void* manager_thread(void* arg) {
    SharedState* state = static_cast<SharedState*>(arg);

    while (state->running.load()) {
        usleep(3'000'000);           
        if (!state->running.load()) break;

        int q    = state->queue.size();
        int curr = state->target_chefs.load();

        if (q >= QUEUE_HIGH_THRESHOLD && curr < MAX_ACTIVE_CHEFS) {
            int next = state->target_chefs.fetch_add(1) + 1;
            log_msg(state,
                    "[Manager] ▲ Queue high (%d orders)  → target chefs: %d → %d\n",
                    q, curr, next);

        } else if (q <= QUEUE_LOW_THRESHOLD && curr > MIN_ACTIVE_CHEFS) {
            int next = state->target_chefs.fetch_sub(1) - 1;
            log_msg(state,
                    "[Manager] ▼ Queue low  (%d orders)  → target chefs: %d → %d\n",
                    q, curr, next);
        }
    }
    return nullptr;
}

void* canceller_thread(void* arg) {
    SharedState* state = static_cast<SharedState*>(arg);
    unsigned int  seed  = static_cast<unsigned int>(time(nullptr)) ^ 0xDEADC0DEu;

    while (state->running.load()) {
        usleep(4'000'000 + static_cast<int>(rand_r(&seed) % 3'000'001));
        if (!state->running.load()) break;

        std::vector<Order> snap = state->queue.snapshot();

        std::vector<int> candidates;
        candidates.reserve(snap.size());
        for (const Order& o : snap) {
            if (!o.cancelled) candidates.push_back(o.id);
        }
        if (candidates.empty()) continue;   

        int chosen_id = candidates[rand_r(&seed) % candidates.size()];

        if (state->queue.cancel_order(chosen_id)) {
            pthread_mutex_lock(&state->stats_mutex);   
            state->total_cancelled++;
            pthread_mutex_unlock(&state->stats_mutex); 

            log_msg(state, "[Canceller] ✗ Cancelled Order #%d\n", chosen_id);
        }
    }
    return nullptr;
}

// ── MAIN APPLICATION ─────────────────────────────────────────────────────────

static SharedState* g_state = nullptr;

static void signal_handler(int /*sig*/) {
    if (g_state) {
        static const char msg[] =
            "\n[System] Signal caught – initiating graceful shutdown...\n";
        ssize_t n = write(STDOUT_FILENO, msg, sizeof(msg) - 1); 
        (void)n;
        g_state->running.store(false);
        g_state->queue.shutdown();  
    }
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

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

    SharedState state;
    g_state = &state;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    state.running.store(true);
    state.start_time = std::chrono::steady_clock::now();

    pthread_t waiter_tids  [NUM_WAITERS];
    pthread_t chef_tids    [NUM_CHEFS];
    pthread_t monitor_tid, manager_tid, canceller_tid;

    WaiterArg waiter_args[NUM_WAITERS];
    ChefArg   chef_args  [NUM_CHEFS];

    for (int i = 0; i < NUM_WAITERS; ++i) {
        waiter_args[i] = {i + 1, &state};
        if (pthread_create(&waiter_tids[i], nullptr,
                           waiter_thread, &waiter_args[i]) != 0) {
            perror("pthread_create waiter");
            return 1;
        }
        printf("[System] Waiter %d started.\n", i + 1);
    }

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

    if (pthread_create(&monitor_tid,   nullptr, monitor_thread,   &state) != 0 ||
        pthread_create(&manager_tid,   nullptr, manager_thread,   &state) != 0 ||
        pthread_create(&canceller_tid, nullptr, canceller_thread, &state) != 0) {
        perror("pthread_create utility");
        return 1;
    }
    printf("[System] Monitor, Manager, and Canceller threads started.\n\n");
    fflush(stdout);

    for (int t = 0; t < SIM_DURATION_SECS && state.running.load(); ++t) {
        sleep(1);
    }

    if (state.running.load()) {          
        printf("\n[System] Simulation time elapsed. Shutting down...\n");
        fflush(stdout);
        state.running.store(false);
        state.queue.shutdown();          
    }

    for (int i = 0; i < NUM_WAITERS; ++i)
        pthread_join(waiter_tids[i], nullptr);

    for (int i = 0; i < NUM_CHEFS; ++i)
        pthread_join(chef_tids[i], nullptr);

    pthread_join(monitor_tid,   nullptr);
    pthread_join(manager_tid,   nullptr);
    pthread_join(canceller_tid, nullptr);

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