#include "shadower/comp/trace_samples/trace_samples.h"
#include "srsran/srslog/srslog.h"
#include "srsran/support/srsran_assert.h"
#include <string.h>
#include <sys/stat.h>
#include <zmq.h>

bool TraceSamples::init(const char* zmq_address)
{
  if (initialized) {
    return true;
  }

  context  = zmq_ctx_new();
  z_socket = zmq_socket(context, ZMQ_PUB);
  int rc   = zmq_bind(z_socket, zmq_address);
  if (rc != 0) {
    logger.error("Error binding to ZMQ socket");
    return false;
  }
  logger.info("ZMQ socket bound to \"%s\"", zmq_address);

  // Fix permission for IPC socket
  if (strstr(zmq_address, "ipc://") != NULL)
    chmod(zmq_address + 6, 0777);

  last_send_time = std::chrono::steady_clock::now();
  initialized    = true;
  return true;
}

void TraceSamples::send(cf_t* samples, uint32_t length, bool ignore_throttle)
{
  if (srsran_unlikely(!z_socket)) {
    logger.error("ZMQ socket not initialized");
    return;
  }

  if (throttle_samples && !ignore_throttle && (send_count++ % throttle_samples)) {
    return;
  }

  if (throttle_time_ms && !ignore_throttle) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_send_time < std::chrono::milliseconds(throttle_time_ms)) {
      return;
    }
    last_send_time = now;
  }

  // logger.info("Sending %u samples to trace", length);
  zmq_send_const(z_socket, samples, length * sizeof(samples), ZMQ_DONTWAIT);
}

void TraceSamples::send_string(const std::string& text, bool ignore_throttle)
{
  if (srsran_unlikely(!z_socket)) {
    logger.error("ZMQ socket not initialized");
    return;
  }

  if (throttle_samples && !ignore_throttle && (send_count++ % throttle_samples)) {
    return;
  }

  if (throttle_time_ms && !ignore_throttle) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_send_time < std::chrono::milliseconds(throttle_time_ms)) {
      return;
    }
    last_send_time = now;
  }

  zmq_send(z_socket, text.c_str(), text.size(), ZMQ_DONTWAIT);
}

TraceSamples::~TraceSamples()
{
  zmq_close(z_socket);
  zmq_ctx_destroy(context);
}