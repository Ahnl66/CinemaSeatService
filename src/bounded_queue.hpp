#pragma once
#include "wire.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace csad {
// Ownership stays within a byte and entry budget, including zero-size controls.
template<class T> class BoundedQueue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::pair<T, size_t>> values;
    size_t bytes = 0, capacity, entries;
    bool cancelled = false;
public:
    BoundedQueue(size_t capacity, size_t entries = 4096) : capacity(capacity), entries(entries) {}
    bool push(T value, size_t size) {
        require(size <= capacity, "limit_exceeded");
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return cancelled || (size <= capacity - bytes && values.size() < entries); });
        if (cancelled) return false;
        values.emplace_back(std::move(value), size); bytes += size; changed.notify_all(); return true;
    }
    bool try_push(T value, size_t size) {
        std::lock_guard lock(mutex);
        if (cancelled || size > capacity - bytes || values.size() >= entries) return false;
        values.emplace_back(std::move(value), size); bytes += size; changed.notify_all(); return true;
    }
    std::optional<T> pop() {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return cancelled || !values.empty(); });
        if (cancelled) return {};
        auto value = std::move(values.front()); values.pop_front(); bytes -= value.second;
        changed.notify_all(); return std::move(value.first);
    }
    void cancel() {
        std::lock_guard lock(mutex);
        cancelled = true; values.clear(); bytes = 0; changed.notify_all();
    }
};
}
