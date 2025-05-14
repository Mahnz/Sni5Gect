#include "srsran/config.h"
#include <memory>
#include <mutex>
#include <stack>
#include <vector>

class SharedBufferPool
{
public:
  SharedBufferPool(size_t buffer_size_, size_t initial_pool_size = 40) : buffer_size(buffer_size_)
  {
    for (uint32_t i = 0; i < initial_pool_size; i++) {
      pool.push(std::make_unique<std::vector<cf_t> >(buffer_size));
    }
  }

  std::shared_ptr<std::vector<cf_t> > get_buffer()
  {
    std::unique_ptr<std::vector<cf_t> > raw_buffer;
    {
      /* Try to retrieve from existing buffer pool */
      std::lock_guard<std::mutex> lock(mutex);
      if (!pool.empty()) {
        raw_buffer = std::move(pool.top());
        pool.pop();
      }
    }
    /* Create a new buffer if no available buffer */
    if (!raw_buffer) {
      raw_buffer = std::make_unique<std::vector<cf_t> >(buffer_size);
    }
    /* Return a shared_ptr with a custom deleter that returns it to the pool */
    return std::shared_ptr<std::vector<cf_t> >(raw_buffer.release(), [this](std::vector<cf_t>* buffer) {
      std::lock_guard<std::mutex> lock(mutex);
      pool.push(std::unique_ptr<std::vector<cf_t> >(buffer));
    });
  }

private:
  size_t                                           buffer_size;
  std::mutex                                       mutex;
  std::stack<std::unique_ptr<std::vector<cf_t> > > pool;
};