#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

namespace base {
  template<typename T>
  class ThreadSafeQueue {
  public:
    ThreadSafeQueue() {}
    ~ThreadSafeQueue() {}

    void Push(T value) {
      std::lock_guard<std::mutex> lock(mtx);
      queue.push(std::move(value));
      condVar.notify_one();
    }

    bool TryPop(T& value) {
      std::lock_guard<std::mutex> lock(mtx);
      if (queue.empty()) {
        return false;
      }
      value = std::move(queue.front());
      queue.pop();
      return true;
    }

    bool TryPopFlex(T& value) {
      std::lock_guard<std::mutex> lock(mtx);
      if (queue.empty()) {
        return false;
      }
      value = std::move(queue.front());
      while (queue.size() > 5) queue.pop();
      if (queue.size() > 1) queue.pop();
      return true;
    }

    bool WaitPop(T& value) {
      std::unique_lock<std::mutex> lock(mtx);
      if (!condVar.wait_for(lock, std::chrono::milliseconds(5), [this] { return !queue.empty(); })) {
        return false;
      }
      value = std::move(queue.front());
      queue.pop();
      return true;
    }

    bool WaitPopFlex(T& value) {
      std::unique_lock<std::mutex> lock(mtx);
      if (!condVar.wait_for(lock, std::chrono::milliseconds(5), [this] { return !queue.empty(); })) {
        return false;
      }
      value = std::move(queue.front());
      while (queue.size() > 5) queue.pop();
      if (queue.size() > 1) queue.pop();
      return true;
    }

    bool Empty() const {
      std::lock_guard<std::mutex> lock(mtx);
      return queue.empty();
    }

    size_t Size() const {
      std::lock_guard<std::mutex> lock(mtx);
      return queue.size();
    }

    void Notify() { condVar.notify_one(); }

  private:
    mutable std::mutex mtx;
    std::queue<T> queue;
    std::condition_variable condVar;
  };
}