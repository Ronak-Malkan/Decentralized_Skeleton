#ifndef THREADSAFE_QUEUE_H
#define THREADSAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class ThreadSafeQueue {
public:
  ThreadSafeQueue() = default;

  // push a new element into the queue
  void push(T value) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      q_.push(std::move(value));
    }
    cond_.notify_one();
  }

  // wait until there’s something and pop it
  T wait_and_pop() {
    std::unique_lock<std::mutex> lk(mtx_);
    cond_.wait(lk, [this]{ return !q_.empty(); });
    T val = std::move(q_.front());
    q_.pop();
    return val;
  }

  // get current size
  size_t size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return q_.size();
  }

private:
  mutable std::mutex             mtx_;
  std::queue<T>                  q_;
  std::condition_variable        cond_;
};

#endif // THREADSAFE_QUEUE_H
