#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H
#include <condition_variable>
#include <deque>
#include <mutex>

template <typename T>
class SafeQueue
{
public:
  void push(std::shared_ptr<T> t)
  {
    std::lock_guard<std::mutex> lock(mtx);
    ts_queue.push_back(t);
    cond_var.notify_one();
  }

  size_t get_size()
  {
    std::lock_guard<std::mutex> lock(mtx);
    return ts_queue.size();
  }

  std::shared_ptr<T> retrieve()
  {
    std::unique_lock<std::mutex> lock(mtx);
    cond_var.wait(lock, [this] { return !ts_queue.empty(); });
    std::shared_ptr<T> t = ts_queue.front();
    ts_queue.pop_front();
    return t;
  }

  std::shared_ptr<T> retrieve_non_blocking()
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (ts_queue.empty()) {
      return nullptr;
    }
    std::shared_ptr<T> t = ts_queue.front();
    ts_queue.pop_front();
    return t;
  }

private:
  std::mutex                      mtx;
  std::condition_variable         cond_var;
  std::deque<std::shared_ptr<T> > ts_queue;
};
#endif // SAFE_QUEUE_H