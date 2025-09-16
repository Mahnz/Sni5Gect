#include "shadower/utils/thread_pool.h"
#include "shadower/utils/utils.h"

ThreadPool::ThreadPool(size_t numThreads) : stop(false)
{
  for (size_t i = 0; i < numThreads; ++i) {
    worker_threads.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(mtx);
          /* Wait for a task or for stop signal */
          cv.wait(lock, [this] { return stop || !tasks.empty(); });
          if (stop && tasks.empty())
            return;

          task = std::move(tasks.front());
          tasks.pop();
        }
        task();
      }
    });
    /* set the thread name */
    pthread_setname_np(worker_threads.back().native_handle(), ("worker-" + std::to_string(i)).c_str());
    /* set the thread priority */
    set_thread_priority(worker_threads.back(), 59);
  }
}

ThreadPool::~ThreadPool()
{
  exit();
}

void ThreadPool::exit()
{
  /* Stop all threads */
  {
    std::unique_lock<std::mutex> lock(mtx);
    stop = true;
  }
  cv.notify_all();
  for (std::thread& worker : worker_threads) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}