/**
 * queue.cpp
 * Implementation of the thread-safe priority order queue.
 *
 * Key synchronisation points are annotated with ← acquire / release comments
 * so that the locking discipline is immediately visible during code review.
 */
#include "queue.h"
#include <algorithm>   // std::remove_if
#include <ctime>       // clock_gettime, CLOCK_REALTIME

// ─────────────────────────── Constructor / Destructor ────────────────────────

OrderQueue::OrderQueue() {
    pthread_mutex_init(&mutex_, nullptr);
    pthread_cond_init(&not_empty_, nullptr);
}

OrderQueue::~OrderQueue() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&not_empty_);
}

// ──────────────────────────── Private Helpers ─────────────────────────────────

void OrderQueue::cleanup_cancelled() {
    // Erase-remove idiom: compacts the vector by removing cancelled entries.
    // PRECONDITION: mutex_ is held by the caller.
    orders_.erase(
        std::remove_if(orders_.begin(), orders_.end(),
                       [](const Order& o) { return o.cancelled; }),
        orders_.end());
}

int OrderQueue::find_best() const {
    // PRECONDITION: mutex_ is held AND cleanup_cancelled() was just called,
    // so every entry in orders_ has cancelled == false.
    if (orders_.empty()) return -1;

    int best = 0;
    for (int i = 1; i < static_cast<int>(orders_.size()); ++i) {
        const Order& a = orders_[i];
        const Order& b = orders_[best];

        // Rule 1: higher numeric Priority value wins (VIP=1 beats NORMAL=0)
        if (static_cast<int>(a.priority) > static_cast<int>(b.priority)) {
            best = i;
            continue;
        }
        // Rule 2: same priority → earlier enqueue_time wins (FIFO)
        if (a.priority == b.priority && a.enqueue_time < b.enqueue_time) {
            best = i;
        }
    }
    return best;
}

// ──────────────────────────── Public Interface ────────────────────────────────

void OrderQueue::enqueue(const Order& order) {
    pthread_mutex_lock(&mutex_);         // ← acquire: exclusive access to orders_
    orders_.push_back(order);
    pthread_cond_signal(&not_empty_);    // wake exactly one waiting Chef thread
    pthread_mutex_unlock(&mutex_);       // ← release
}

bool OrderQueue::dequeue(Order& order, const std::atomic<bool>& running) {
    pthread_mutex_lock(&mutex_);         // ← acquire

    while (running.load()) {
        // Remove stale cancelled entries before searching for the best order.
        cleanup_cancelled();

        if (!orders_.empty()) {
            int idx = find_best();       // guaranteed >= 0 since orders_ is non-empty
            order = orders_[idx];
            orders_.erase(orders_.begin() + idx);
            pthread_mutex_unlock(&mutex_);   // ← release (success path)
            return true;
        }

        // Queue is empty – sleep until signalled or timed out.
        // pthread_cond_timedwait atomically: releases mutex_ → sleeps → re-acquires.
        // The 200 ms timeout lets us periodically re-check 'running' even if
        // no signal arrives (e.g., during shutdown after the last order is taken).
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200'000'000L;     // +200 ms
        if (ts.tv_nsec >= 1'000'000'000L) {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1'000'000'000L;
        }
        pthread_cond_timedwait(&not_empty_, &mutex_, &ts);
        // After returning, mutex_ is re-held; loop and re-check running + queue.
    }

    pthread_mutex_unlock(&mutex_);       // ← release (shutdown path)
    return false;
}

bool OrderQueue::cancel_order(int order_id) {
    pthread_mutex_lock(&mutex_);         // ← acquire
    bool found = false;
    for (Order& o : orders_) {
        if (o.id == order_id && !o.cancelled) {
            o.cancelled = true;          // mark; will be cleaned up on next dequeue
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_);       // ← release
    return found;
}

int OrderQueue::size() {
    pthread_mutex_lock(&mutex_);         // ← acquire
    int count = 0;
    for (const Order& o : orders_) {
        if (!o.cancelled) ++count;
    }
    pthread_mutex_unlock(&mutex_);       // ← release
    return count;
}

std::vector<Order> OrderQueue::snapshot() {
    pthread_mutex_lock(&mutex_);         // ← acquire
    std::vector<Order> copy = orders_;  // deep copy (includes cancelled entries;
                                        // caller filters as needed)
    pthread_mutex_unlock(&mutex_);       // ← release
    return copy;
}

void OrderQueue::shutdown() {
    pthread_mutex_lock(&mutex_);         // ← acquire
    pthread_cond_broadcast(&not_empty_); // wake ALL threads blocked in dequeue()
    pthread_mutex_unlock(&mutex_);       // ← release
}
