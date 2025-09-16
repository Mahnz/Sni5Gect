#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
  explicit ThreadPool(size_t numThreads);
  ~ThreadPool();
  void exit();
  // Add a new task to the pool
  template <class F, class... Args>
  auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
  {
    using returnType = typename std::result_of<F(Args...)>::type;
    auto task =
        std::make_shared<std::packaged_task<returnType()> >(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<returnType> result = task->get_future();
    {
      std::unique_lock<std::mutex> lock(mtx);
      if (stop)
        throw std::runtime_error("Thread pool has stopped.");
      tasks.emplace([task]() { (*task)(); });
    }
    cv.notify_one();
    return result;
  }

private:
  // threads
  std::vector<std::thread> worker_threads;
  // Task queue
  std::queue<std::function<void()> > tasks;
  // Synchronization
  std::mutex              mtx;
  std::condition_variable cv;
  std::atomic<bool>       stop;
};

#endif // THREAD_POOL_H