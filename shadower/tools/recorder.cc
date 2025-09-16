#include "shadower/hdr/arg_parser.h"
#include "shadower/hdr/buffer_pool.h"
#include "shadower/hdr/constants.h"
#include "shadower/hdr/source.h"
#include "srsran/phy/rf/rf.h"
#include "srsran/phy/utils/vector.h"
#include "srsran/radio/rf_buffer.h"
#include "srsran/srsran.h"
#include <atomic>
#include <complex>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <iostream>
#include <liquid/liquid.h>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <sched.h>
#include <string>
#include <uhd/usrp/multi_usrp.hpp>
#include <vector>
using namespace std;
atomic<bool> stop_flag(false);
typedef struct {
  double                                        freq;
  double                                        srate;
  uint32_t                                      frames;
  queue<shared_ptr<vector<complex<float> > > >* buffer_queue;
  mutex*                                        mtx;
  condition_variable*                           cv;
  msresamp_crcf                                 resampler;
  string                                        outputFileName;
} sdr_params_t;

const double gain         = 40;   // dB
const double subframeTime = 1e-3; // 1 ms
string       device_args  = "type=b200";
// const string device_args =
//     "type=x300,addr=192.168.40.2,sampling_rate=200e6,send_frame_size=8000,recv_frame_size=8000,clock=internal";
bool   enable_resampler = false;
double sdr_srate        = 23.04e6;

struct frame_t {
  uint32_t                            frames_idx;
  std::shared_ptr<std::vector<cf_t> > buffer[SRSRAN_MAX_CHANNELS];
  size_t                              buffer_size;
};

SharedBufferPool*                     buffer_pool = nullptr;
std::queue<std::shared_ptr<frame_t> > queue;
std::condition_variable               cv;
std::mutex                            mtx;

double         center_freq      = 3427.5e6;
double         sample_rate      = 23.04e6;
double         sdr_sample_rate  = 23.04e6;
double         gain             = 40;
uint32_t       num_frames       = 20000;
uint32_t       num_channels     = 1;
std::string    output_file      = "output";
std::string    output_folder    = "/root/records/";
std::string    source_type      = "uhd";
std::string    device_args      = "type=b200";
bool           enable_resampler = false;
double         resample_rate    = 1.0;
ShadowerConfig config           = {};
uint32_t       sf_len           = 0;
uint32_t       sf_len_sdr       = 0;

void sigint_handler(int signum)
{
  auto* sdr_params = (sdr_params_t*)args;

  queue<shared_ptr<vector<complex<float> > > >* queue  = sdr_params->buffer_queue;
  double                                        freq   = sdr_params->freq;
  double                                        srate  = sdr_params->srate;
  uint32_t                                      frames = sdr_params->frames;
  mutex*                                        mtx    = sdr_params->mtx;
  condition_variable*                           cv     = sdr_params->cv;

  // create a USRP device
  uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
  usrp->set_rx_rate(sdr_srate);
  usrp->set_rx_freq(freq);
  usrp->set_rx_gain(gain);
  usrp->set_clock_source("internal");
  const size_t buffer_size = srate * subframeTime;
  // Create a receiver streamer
  uhd::rx_streamer::sptr stream = usrp->get_rx_stream(uhd::stream_args_t("fc32", "sc16"));
  // Create a receive metadata structure
  uhd::rx_metadata_t metadata;
  // Start streaming
  uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
  stream_cmd.num_samps  = 0;
  stream_cmd.stream_now = true;
  stream->issue_stream_cmd(stream_cmd);

  long subFrameCount = 0;

  while (subFrameCount < frames && !stop_flag.load()) {
    try {
      shared_ptr<vector<complex<float> > > buffer = make_shared<vector<complex<float> > >(buffer_size);
      stream->recv(buffer->data(), buffer_size, metadata, 3.0);
      subFrameCount++;
      {
        lock_guard<mutex> lock(*mtx);
        queue->push(buffer);
      }
      cv->notify_one();
    } catch (const exception& e) {
      break;
    }
  }
  stop_flag.store(true);
  printf("Received signal %d, stopping...\n", signum);
}

void parse_args(int argc, char* argv[])
{
  int opt;
  int option_index = 0;

  ofstream outfile(sdr_params->outputFileName, ios::binary);
  if (!outfile.is_open()) {
    cerr << "Failed to open output file" << endl;
    stop_flag.store(true);
    return;
  }
  long                              numSubframes = 0;
  std::vector<std::complex<float> > output_buffer(sdr_params->srate * subframeTime);
  while (!stop_flag.load()) {
    shared_ptr<vector<complex<float> > > buffer;
    {
      unique_lock<mutex> lock(*mtx);
      cv->wait(lock, [queue] { return !queue->empty(); });
      buffer = queue->front();
      queue->pop();
    }

    if (buffer == nullptr) {
      break;
    }

    if (enable_resampler) {
      uint32_t num_output_samples;
      msresamp_crcf_execute(sdr_params->resampler,
                            (liquid_float_complex*)buffer->data(),
                            buffer->size(),
                            (liquid_float_complex*)output_buffer.data(),
                            &num_output_samples);
      outfile.write(reinterpret_cast<char*>(output_buffer.data()), num_output_samples * sizeof(complex<float>));
    } else {
      outfile.write(reinterpret_cast<char*>(buffer->data()), buffer->size() * sizeof(complex<float>));
    }

    if (numSubframes++ % 50 == 0) {
      printf(".");
      fflush(stdout);
    }
  }
}

int main(int argc, char* argv[])
{
  double   centerFrequency = 3424.5e6; // 3424.5 MHz
  double   sampleRate      = 46.08e6;  // 46.08 MHz
  string   outputFile      = "output.fc32";
  uint32_t num_frames      = 1200000;

  if (argc > 1) {
    double centerFrequencyMHz = atof(argv[1]);
    centerFrequency           = centerFrequencyMHz * 1e6;
  }

  if (argc > 2) {
    num_frames = atoi(argv[2]);
  }

  if (argc > 3) {
    outputFile = argv[3];
  }

  if (argc > 4) {
    double sampleRateMHz = atof(argv[4]);
    sampleRate           = sampleRateMHz * 1e6;
  }

  if (argc > 5) {
    double sdr_srateMHz = atof(argv[5]);
    sdr_srate           = sdr_srateMHz * 1e6;
    if (sdr_srate < sampleRate) {
      cerr << "SDR sample rate must be greater than or equal to the sample rate" << endl;
      return -1;
    }
    if (sdr_srate != sampleRate) {
      enable_resampler = true;
    }
  } else {
    sdr_srate = sampleRate;
  }

  if (argc > 6) {
    device_args = argv[6];
  }

  queue<shared_ptr<vector<complex<float> > > > buffer_queue;
  mutex                                        mtx;
  condition_variable                           cv;

  float         resample_rate = sampleRate / sdr_srate;
  msresamp_crcf resampler     = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);
  printf("Using sample rate: %f\n", sdr_srate);
  if (enable_resampler) {
    printf("Using resampling rate: %f\n", resample_rate);
  }

  pthread_t    receiver_thread, writer_thread;
  sdr_params_t params = {centerFrequency, sampleRate, num_frames * 10, &buffer_queue, &mtx, &cv, resampler, outputFile};
  // create receiver thread
  if (pthread_create(&receiver_thread, nullptr, read_from_sdr, (void*)&params)) {
    cerr << "Error creating receiver thread" << endl;
    return -1;
  }
  // set the receiver thread to the highest priority
  struct sched_param sp{};
  sp.sched_priority = sched_get_priority_max(SCHED_RR);
  if (pthread_setschedparam(receiver_thread, SCHED_RR, &sp) != 0) {
    fprintf(stderr, "Failed to set receiver thread priority\n");
    return -1;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(3, &cpuset);
  if (pthread_setaffinity_np(receiver_thread, sizeof(cpuset), &cpuset) != 0) {
    fprintf(stderr, "Failed to set receiver thread affinity\n");
    return -1;
  }
  // set the writer thread to the lowest priority
  struct sched_param sp2{};
  sp2.sched_priority = sched_get_priority_min(SCHED_IDLE);
  if (pthread_setschedparam(writer_thread, SCHED_IDLE, &sp2) != 0) {
    cerr << "Failed to set thread priority" << endl;
  }

  int nproc = sysconf(_SC_NPROCESSORS_ONLN);
  for (int i = 0; i < nproc; i++) {
    printf("%d ", CPU_ISSET(i, &cpuset));
  }
  printf("\n");
  // Create writer thread
  if (pthread_create(&writer_thread, nullptr, (void* (*)(void*))writer_worker, nullptr) != 0) {
    fprintf(stderr, "Failed to create writer thread\n");
    return -1;
  }
  pthread_join(receiver_thread, nullptr);
  pthread_join(writer_thread, nullptr);
  msresamp_crcf_destroy(resampler);
  return 0;
}