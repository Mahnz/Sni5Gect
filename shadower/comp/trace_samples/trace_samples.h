#ifndef TRACE_SAMPLES_H
#define TRACE_SAMPLES_H
#include "srsran/config.h"
#include "srsran/srslog/srslog.h"
class TraceSamples
{
public:
  TraceSamples() = default;
  ~TraceSamples();
  bool init(const char* zmq_address);
  void send(cf_t* samples, uint32_t length, bool ignore_throttle = false);
  void send_string(const std::string& text, bool ignore_throttle = false);
  void set_throttle(uint32_t throttle_samples) { this->throttle_samples = throttle_samples; };
  void set_throttle_ms(uint32_t throttle_time_) { this->throttle_time_ms = throttle_time_; };
  void reset_throttle() { this->send_count = 0; };
  bool initialized = false;

private:
  uint32_t              send_count       = 0;
  srslog::basic_logger& logger           = srslog::fetch_basic_logger("TraceSamples");
  void*                 context          = nullptr;
  void*                 z_socket         = nullptr;
  uint32_t              throttle_samples = 0; // Throttle the number of samples sent to the trace
  uint32_t              throttle_time_ms = 0;
  std::chrono::time_point<std::chrono::steady_clock> last_send_time;
};

#endif // TRACE_SAMPLES_H