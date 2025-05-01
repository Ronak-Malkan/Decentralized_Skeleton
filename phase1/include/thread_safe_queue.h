#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;

    // Enqueue an item and notify one waiting thread
    void push(T item) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            q_.push(std::move(item));
        }
        cv_.notify_one();
    }

    // Block until there’s an item, then pop and return it
    T wait_and_pop() {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this]{ return !q_.empty(); });
        T item = std::move(q_.front());
        q_.pop();
        return item;
    }

    // Try pop without blocking; returns false if empty
    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    // Return the number of items currently queued
    size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.size();
    }

private:
    mutable std::mutex        mtx_;  // mutable so size() can lock in const context
    std::condition_variable   cv_;
    std::queue<T>             q_;
};

#endif // THREAD_SAFE_QUEUE_H
