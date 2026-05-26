/**
 * queue.h
 * Thread-safe priority queue for Order objects.
 *
 * Synchronisation design:
 *   mutex_      – guards all reads and writes to the internal order vector.
 *   not_empty_  – condition variable; chefs block here when the queue is
 *                 empty and are woken when a new order arrives (enqueue)
 *                 or when shutdown() is called.
 *
 * Priority policy:
 *   VIP orders are always dequeued before NORMAL orders.
 *   Within the same priority tier, FIFO ordering is preserved
 *   (earlier enqueue_time wins).
 *
 * Cancellation:
 *   cancel_order() marks an order as cancelled while it is still in the
 *   vector.  cleanup_cancelled() lazily removes marked entries each time
 *   dequeue() is about to search for the best order, keeping the
 *   bookkeeping cost amortised.
 */
#pragma once

#include <vector>
#include <pthread.h>
#include <atomic>
#include "order.h"

class OrderQueue {
public:
    OrderQueue();
    ~OrderQueue();

    /**
     * Enqueue an order and wake one waiting consumer.
     * Thread-safe; may be called from multiple Waiter threads concurrently.
     */
    void enqueue(const Order& order);

    /**
     * Blocking dequeue.
     * Returns the highest-priority uncancelled order, or blocks (with a
     * 200 ms timeout) when the queue is empty.
     *
     * @param order   [out] Receives the dequeued order on success.
     * @param running Checked on every wake-up; return false when false.
     * @return true  if an order was successfully obtained.
     *         false on shutdown (running == false).
     */
    bool dequeue(Order& order, const std::atomic<bool>& running);

    /**
     * Mark an in-queue order as cancelled.
     * @return true if found and cancelled; false if the order was not found
     *         (already dequeued or never existed).
     * Thread-safe.
     */
    bool cancel_order(int order_id);

    /** Returns the count of non-cancelled pending orders. Thread-safe. */
    int size();

    /**
     * Returns a snapshot copy of the internal order list.
     * Used by the Canceller thread to choose a cancellation candidate and
     * by the Monitor thread to display queue contents.
     * Thread-safe.
     */
    std::vector<Order> snapshot();

    /**
     * Broadcast on not_empty_ to wake all threads blocked in dequeue().
     * Called during shutdown so that threads can observe running == false
     * and exit promptly.
     */
    void shutdown();

private:
    std::vector<Order> orders_;    ///< Backing store – unsorted; best found by linear scan
    pthread_mutex_t    mutex_;     ///< Protects orders_ and the condition variable
    pthread_cond_t     not_empty_; ///< Signalled whenever a new order is enqueued

    /** Remove all orders where cancelled == true. Caller must hold mutex_. */
    void cleanup_cancelled();

    /**
     * Find the index of the highest-priority order in orders_.
     * Assumes: mutex_ is held AND cleanup_cancelled() has just been called
     *          so there are no cancelled entries.
     * Returns -1 if orders_ is empty.
     */
    int find_best() const;
};
