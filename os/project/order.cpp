/**
 * order.cpp
 * Implementation of Order constructors.
 */
#include "order.h"

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
