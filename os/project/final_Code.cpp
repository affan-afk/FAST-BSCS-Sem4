#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <atomic>
#include <vector>
using namespace std;

#define NUM_WAITERS 3    
#define NUM_CHEFS 5    
#define MAX_KITCHEN 4    
#define SIM_TIME 30  

#define VIP 1
#define NORMAL 0

struct Order{
    int id;
    int prep_time;  
    int priority;    
    time_t enqueue_time;
};

struct OrderQueue{
    vector<Order> orders;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

OrderQueue g_queue;

atomic<int> running(1);
atomic<int> next_id(1);

int total_completed = 0;
int total_cancelled = 0;
pthread_mutex_t stats_mutex;   
pthread_mutex_t print_mutex;
sem_t kitchen_sem;

struct WaiterArg{ 
    int id;
};
struct ChefArg{ 
    int id;
};

void log_msg(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    pthread_mutex_lock(&print_mutex);
    printf("%s", buf);
    fflush(stdout);
    pthread_mutex_unlock(&print_mutex);
}

void enqueue(Order o) {
    pthread_mutex_lock(&g_queue.mutex);
    g_queue.orders.push_back(o);
    pthread_cond_signal(&g_queue.cond); 
    pthread_mutex_unlock(&g_queue.mutex);
}

int find_best() {
    int best = 0;
    for (int i=1;i < (int)g_queue.orders.size(); i++){
        Order& a = g_queue.orders[i];
        Order& b = g_queue.orders[best];

        if (a.priority>b.priority) {
            best = i;
        }else if(a.priority == b.priority && a.enqueue_time < b.enqueue_time) {
            best = i;
        }
    }
    return best;
}

int dequeue(Order* o){
    pthread_mutex_lock(&g_queue.mutex);

    while (g_queue.orders.empty() && running.load()) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200000000L; 
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&g_queue.cond, &g_queue.mutex, &ts);
    }

    if(g_queue.orders.empty()) {
        pthread_mutex_unlock(&g_queue.mutex);
        return 0;
    }

    int idx = find_best();
    *o = g_queue.orders[idx];
    g_queue.orders.erase(g_queue.orders.begin() + idx);

    pthread_mutex_unlock(&g_queue.mutex);
    return 1;
}

void* waiter(void* arg) {
    WaiterArg* wa = (WaiterArg*)arg;
    int id = wa->id;

    unsigned int seed = time(NULL) ^ id;

    while (running.load()){
        Order o;
        o.id = next_id.fetch_add(1);   
        o.prep_time = 500 + (int)(rand_r(&seed) % 2001);
        if(rand_r(&seed) % 4 == 0){
            o.priority = VIP;
        }else{
            o.priority = NORMAL;
        }
        o.enqueue_time = time(NULL);

        enqueue(o);
        if (o.priority == VIP)
        {
            log_msg("[Waiter %d] New Order  #%-3d  (%s)\n",id, o.id, "VIP");
        }
        else
        {
            log_msg("[Waiter %d] New Order  #%-3d  (%s)\n",id, o.id, "NORMAL");
        }
    
        usleep(600000 + (unsigned int)(rand_r(&seed) % 1000001));
    }

    log_msg("[Waiter %d] Done.\n", id);
    return NULL;
}


void* chef(void* arg) {
    ChefArg* ca = (ChefArg*)arg;
    int id = ca->id;

    while(running.load()) {
        Order o;

        if (dequeue(&o) == 0){
            break;
        }

        sem_wait(&kitchen_sem);

        if (o.priority == VIP)
        {
            log_msg("[Chef %d] Picked Order #%-3d  (%s)\n",id, o.id, "VIP");
        }
        else
        {
            log_msg("[Chef %d] Picked Order #%-3d  (%s)\n",id, o.id, "NORMAL");
        }
        
        log_msg("[Chef  %d] Processing Order #%-3d  (prep: %d ms)\n",id, o.id, o.prep_time);

        usleep((useconds_t)o.prep_time * 1000);

        log_msg("[Chef  %d] Completed  Order #%-3d\n", id, o.id);

        pthread_mutex_lock(&stats_mutex);
        total_completed++;
        pthread_mutex_unlock(&stats_mutex);

        sem_post(&kitchen_sem);
    }

    log_msg("[Chef  %d] Done.\n", id);
    return NULL;
}


int main() {
    srand(time(NULL));

    pthread_mutex_init(&g_queue.mutex, NULL);
    pthread_cond_init (&g_queue.cond, NULL);
    pthread_mutex_init(&stats_mutex, NULL);
    pthread_mutex_init(&print_mutex, NULL);

    sem_init(&kitchen_sem, 0, MAX_KITCHEN);

    pthread_t waiter_tids[NUM_WAITERS];
    pthread_t chef_tids[NUM_CHEFS];
    WaiterArg wargs[NUM_WAITERS];
    ChefArg cargs[NUM_CHEFS];

    printf("=== Restaurant Order Management Simulator ===\n");
    printf("Waiters: %d  |  Chefs: %d  |  Kitchen slots: %d  |  Duration: %ds\n\n",NUM_WAITERS, NUM_CHEFS, MAX_KITCHEN, SIM_TIME);


    for (int i = 0;i<NUM_WAITERS;i++){
        wargs[i].id = i+1;
        pthread_create(&waiter_tids[i], NULL, waiter, &wargs[i]);
    }


    for (int i = 0; i < NUM_CHEFS; i++) {
        cargs[i].id = i + 1;
        pthread_create(&chef_tids[i], NULL, chef, &cargs[i]);
    }

    sleep(SIM_TIME);
    running.store(0);

    pthread_mutex_lock(&g_queue.mutex);
    pthread_cond_broadcast(&g_queue.cond);
    pthread_mutex_unlock(&g_queue.mutex);
   
    for(int i=0; i < NUM_WAITERS; i++){
        pthread_join(waiter_tids[i], NULL); 
    } 
    for(int i=0; i < NUM_CHEFS;   i++){
        pthread_join(chef_tids[i], NULL);
    }

    printf("\n========== FINAL REPORT ==========\n");
    printf("Orders Completed : %d\n", total_completed);
    printf("Orders Remaining : %d\n", (int)g_queue.orders.size());
    printf("==================================\n");


    pthread_mutex_destroy(&g_queue.mutex);
    pthread_cond_destroy (&g_queue.cond);
    pthread_mutex_destroy(&stats_mutex);
    pthread_mutex_destroy(&print_mutex);
    sem_destroy(&kitchen_sem);

    return 0;
}