#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_RUNWAYS 3
#define MAX_FLIGHTS 200
#define WAIT_THRESHOLD 5

typedef enum {
    EMERGENCY_LAND = 0,
    LOW_FUEL_LAND,
    LANDING,
    CARGO_TAKEOFF,
    TAKEOFF
} FlightType;

const char *flightTypeStr[] = {
    "EMERGENCY", "LOW_FUEL", "LANDING", "CARGO", "TAKEOFF"
};

typedef struct {
    int id;
    int priority;
    int preempted;
    FlightType type;
    time_t arrivalTime;
} Flight;

typedef struct {
    Flight *data[MAX_FLIGHTS];
    int size;
} PriorityQueue;

typedef struct {
    int id;
    int busy;
    int underMaintenance;
    Flight *currentFlight;
} Runway;

PriorityQueue queue = {0};
Runway runways[NUM_RUNWAYS];

pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t runwayMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t flightAvailable = PTHREAD_COND_INITIALIZER;
pthread_cond_t runwayFree = PTHREAD_COND_INITIALIZER;

int systemRunning = 1;
int nextFlightId = 1;

void swap(int i, int j) {
    Flight *t = queue.data[i];
    queue.data[i] = queue.data[j];
    queue.data[j] = t;
}

void push(Flight *f) {
    int i = queue.size;
    queue.size++;
    queue.data[i] = f;

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (queue.data[parent]->priority <= queue.data[i]->priority) {
            break;
        }
        swap(i, parent);
        i = parent;
    }
}

Flight* pop() {
    if (queue.size == 0) {
        return NULL;
    }

    Flight *top = queue.data[0];
    queue.size--;
    queue.data[0] = queue.data[queue.size];

    for (int i = 0; ; ) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < queue.size && queue.data[left]->priority < queue.data[smallest]->priority) {
            smallest = left;
        }

        if (right < queue.size && queue.data[right]->priority < queue.data[smallest]->priority) {
            smallest = right;
        }

        if (smallest == i) {
            break;
        }

        swap(i, smallest);
        i = smallest;
    }
    return top;
}

void adjustPriorities() {
    time_t now = time(NULL);

    for (int i = 0; i < queue.size; i++) {
        if (difftime(now, queue.data[i]->arrivalTime) > WAIT_THRESHOLD && queue.data[i]->priority > 0) {
            queue.data[i]->priority--;
        }
    }
}

void* flightGenerator(void *arg) {
    while (systemRunning) {
        sleep(1);

        Flight *f = malloc(sizeof(Flight));
        f->id = nextFlightId;
        nextFlightId++;
        f->type = rand() % 5;
        f->priority = f->type;
        f->arrivalTime = time(NULL);
        f->preempted = 0;

        pthread_mutex_lock(&queueMutex);
        push(f);
        printf("[Generator] Flight %d (%s, p=%d)\n", f->id, flightTypeStr[f->type], f->priority);
        pthread_cond_signal(&flightAvailable);
        pthread_mutex_unlock(&queueMutex);
    }
    return NULL;
}

void* runwayController(void *arg) {
    int id = *(int*)arg;
    free(arg);

    while (systemRunning) {
        pthread_mutex_lock(&queueMutex);

        while (queue.size == 0 && systemRunning) {
            pthread_cond_wait(&flightAvailable, &queueMutex);
        }

        if (!systemRunning) {
            pthread_mutex_unlock(&queueMutex);
            break;
        }

        adjustPriorities();
        Flight *f = pop();
        pthread_mutex_unlock(&queueMutex);

        pthread_mutex_lock(&runwayMutex);
        while (runways[id].busy || runways[id].underMaintenance) {
            pthread_cond_wait(&runwayFree, &runwayMutex);
        }

        if (runways[id].underMaintenance) {
            pthread_mutex_unlock(&runwayMutex);

            pthread_mutex_lock(&queueMutex);
            push(f);
            pthread_mutex_unlock(&queueMutex);
            continue;
        }

        runways[id].busy = 1;
        runways[id].currentFlight = f;
        pthread_mutex_unlock(&runwayMutex);

        printf("[Runway %d] Flight %d (%s)\n", id, f->id, flightTypeStr[f->type]);

        sleep(2);
        free(f);

        pthread_mutex_lock(&runwayMutex);
        runways[id].busy = 0;
        runways[id].currentFlight = NULL;
        pthread_cond_broadcast(&runwayFree);
        pthread_mutex_unlock(&runwayMutex);
    }
    return NULL;
}

void rebuildHeap() {
    for (int i = queue.size / 2 - 1; i >= 0; i--) {
        for (int j = i; ; ) {
            int l = 2 * j + 1;
            int r = 2 * j + 2;
            int s = j;

            if (l < queue.size && queue.data[l]->priority < queue.data[s]->priority) {
                s = l;
            }
            if (r < queue.size && queue.data[r]->priority < queue.data[s]->priority) {
                s = r;
            }

            if (s == j) {
                break;
            }

            swap(j, s);
            j = s;
        }
    }
}

void* emergencyMonitor(void *arg) {
    while (systemRunning) {
        sleep(2);

        pthread_mutex_lock(&queueMutex);

        for (int i = 0; i < queue.size; i++) {
            if (queue.data[i]->type == EMERGENCY_LAND) {
                queue.data[i]->priority = -1;
                printf("[Emergency] Flight %d prioritized\n", queue.data[i]->id);
                rebuildHeap();

                pthread_mutex_lock(&runwayMutex);
                for (int r = 0; r < NUM_RUNWAYS; r++) {
                    if (runways[r].busy && runways[r].currentFlight && runways[r].currentFlight->type != EMERGENCY_LAND) {
                        printf("[Preempt] Runway %d\n", r);
                        push(runways[r].currentFlight);

                        runways[r].busy = 0;
                        runways[r].currentFlight = NULL;
                        pthread_cond_broadcast(&runwayFree);
                        break;
                    }
                }
                pthread_mutex_unlock(&runwayMutex);
                break;
            }
        }

        if (rand() % 8 == 0) {
            int r = rand() % NUM_RUNWAYS;

            pthread_mutex_lock(&runwayMutex);
            if (!runways[r].busy) {
                runways[r].underMaintenance = 1;
                printf("[Maintenance] Runway %d\n", r);

                sleep(3);

                runways[r].underMaintenance = 0;
                pthread_cond_broadcast(&runwayFree);
            }
            pthread_mutex_unlock(&runwayMutex);
        }

        pthread_mutex_unlock(&queueMutex);
    }
    return NULL;
}

int main() {
    srand(time(NULL));

    for (int i = 0; i < NUM_RUNWAYS; i++) {
        runways[i] = (Runway){i, 0, 0, NULL};
    }

    pthread_t generatorThread;
    pthread_t emergencyThread;
    pthread_t controllers[NUM_RUNWAYS];

    pthread_create(&generatorThread, NULL, flightGenerator, NULL);
    pthread_create(&emergencyThread, NULL, emergencyMonitor, NULL);

    for (int i = 0; i < NUM_RUNWAYS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&controllers[i], NULL, runwayController, id);
    }

    sleep(20);
    systemRunning = 0;

    pthread_cond_broadcast(&flightAvailable);
    pthread_cond_broadcast(&runwayFree);

    pthread_join(generatorThread, NULL);
    pthread_join(emergencyThread, NULL);

    for (int i = 0; i < NUM_RUNWAYS; i++) {
        pthread_join(controllers[i], NULL);
    }

    printf("\nSimulation Complete\n");
    return 0;
}