/**
 * order.h
 * Defines the Order structure and Priority enum used throughout the simulator.
 */
#pragma once

#include <chrono>

// ── Priority Levels ────────────────────────────────────────────────────────────
enum class Priority {
    NORMAL = 0,
    VIP    = 1   ///< VIP orders are always served before NORMAL orders
};

/// Returns a human-readable string for the given priority level
inline const char* priority_str(Priority p) {
    return (p == Priority::VIP) ? "VIP" : "NORMAL";
}

// ── Order ──────────────────────────────────────────────────────────────────────
/**
 * Represents a single customer order in the restaurant system.
 * Orders are produced by Waiter threads and consumed by Chef threads.
 */
struct Order {
    int      id;            ///< Unique, monotonically-increasing order identifier
    int      prep_time_ms;  ///< Time (ms) required to prepare this order
    Priority priority;      ///< NORMAL or VIP — determines queue position
    std::chrono::steady_clock::time_point enqueue_time; ///< When the order entered the queue
    bool     cancelled;     ///< Set to true by the Canceller thread to abort the order

    Order();
    Order(int id, int prep_time_ms, Priority priority);
};
