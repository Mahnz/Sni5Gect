#include "shadower/hdr/arg_parser.h"
#include "shadower/hdr/buffer_pool.h"
#include "shadower/hdr/constants.h"
#include "shadower/hdr/source.h"
#include "srsran/phy/utils/vector.h"
#include <atomic>
#include <complex>
#include <condition_variable>
#include <csignal>
#include <getopt.h>
#include <liquid/liquid.h>
#include <queue>

std::atomic<bool> stop_flag(false);

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
  if (signum == SIGINT) {
    printf("Received SIGINT, stopping...\n");
  } else if (signum == SIGTERM) {
    printf("Received SIGTERM, stopping...\n");
  }
  stop_flag.store(true);
  printf("Received signal %d, stopping...\n", signum);
}

void parse_args(int argc, char* argv[])
{
  int opt;
  int option_index = 0;

  static struct option long_options[] = {

      {"frequency", required_argument, 0, 'f'},
      {"srate", required_argument, 0, 's'},
      {"sdr-srate", required_argument, 0, 'S'},
      {"gain", required_argument, 0, 'g'},
      {"frames", required_argument, 0, 'n'},
      {"channels", required_argument, 0, 'c'},
      {"output", required_argument, 0, 'o'},
      {"folder", required_argument, 0, 'F'},
      {"source-type", required_argument, 0, 't'},
      {"device-args", required_argument, 0, 'd'},
      {0, 0, 0, 0}

  };

  while ((opt = getopt_long(argc, argv, "f:s:S:g:n:c:o:F:t:d:", long_options, &option_index)) != -1) {
    switch (opt) {
      case 'f': {
        double center_freq_MHz = atof(optarg);
        center_freq            = center_freq_MHz * 1e6;
        break;
      }
      case 's': {
        double sample_rate_MHz = atof(optarg);
        sample_rate            = sample_rate_MHz * 1e6;
        break;
      }
      case 'S': {
        double sdr_sample_rate_MHz = atof(optarg);
        sdr_sample_rate            = sdr_sample_rate_MHz * 1e6;
        enable_resampler           = true; // Enable resampling if sdr_sample_rate is different from sample_rate
        break;
      }
      case 'g':
        gain = atof(optarg);
        break;
      case 'n':
        num_frames = atoi(optarg);
        break;
      case 'c':
        num_channels = atoi(optarg);
        break;
      case 'o':
        output_file = optarg;
        break;
      case 'F':
        output_folder = optarg;
        break;
      case 't':
        source_type = optarg;
        break;
      case 'd':
        device_args = optarg;
        break;
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }

  printf("Using Center Frequency: %f MHz\n", center_freq / 1e6);
  printf("      Sample Rate: %f MHz\n", sample_rate / 1e6);
  enable_resampler = sdr_sample_rate != sample_rate;
  if (enable_resampler) {
    printf("      SDR Sample Rate: %f MHz\n", sdr_sample_rate / 1e6);
    resample_rate = sample_rate / sdr_sample_rate;
    printf("      Resampling Rate: %f\n", resample_rate);
  }
  printf("      Gain: %f db\n", gain);
  printf("      Number of Channels: %d\n", num_channels);
  printf("      Device Args: %s\n", device_args.c_str());
  output_file = output_folder + output_file;
  if (num_channels == 1) {
    printf("      Output File: %s\n", output_file.c_str());
  }
  config.sample_rate   = sample_rate;
  config.source_srate  = sdr_sample_rate;
  config.dl_freq       = center_freq;
  config.ul_freq       = center_freq;
  config.rx_gain       = gain;
  config.tx_gain       = gain;
  config.nof_channels  = num_channels;
  config.source_params = device_args;
  sf_len               = sample_rate * SF_DURATION;
  sf_len_sdr           = sdr_sample_rate * SF_DURATION;
}

void receiver_worker()
{
  /* Initialize Source */
  Source* source = nullptr;

  if (source_type == "uhd") {
    create_source_t create_source = load_source(uhd_source_module_path);
    source                        = create_source(config);
  } else {
    fprintf(stderr, "Unknown source type: %s\n", source_type.c_str());
    exit(EXIT_FAILURE);
  }

  buffer_pool              = new SharedBufferPool(sf_len_sdr, 100);
  srsran_timestamp_t ts    = {};
  uint32_t           count = 0;
  while (count++ < num_frames && !stop_flag.load()) {
    std::shared_ptr<frame_t> frame = std::make_shared<frame_t>();
    cf_t*                    rx_buffer[SRSRAN_MAX_CHANNELS];
    frame->frames_idx = count;

    for (uint32_t i = 0; i < num_channels; i++) {
      std::shared_ptr<std::vector<cf_t> > buf = buffer_pool->get_buffer();
      frame->buffer[i]                        = buf;
      rx_buffer[i]                            = buf->data();
    }

    int result = source->recv(rx_buffer, sf_len_sdr, &ts);
    if (result == -1) {
      fprintf(stderr, "Failed to receive samples\n");
      break;
    }
    frame->buffer_size = result;
    {
      std::lock_guard<std::mutex> lock(mtx);
      queue.push(frame);
      cv.notify_one();
    }
  }
  stop_flag.store(true);
  source->close();
}

void writer_worker()
{
  // Create output files
  std::ofstream outfiles[SRSRAN_MAX_CHANNELS];
  if (num_channels > 1) {
    for (uint32_t i = 0; i < num_channels; i++) {
      outfiles[i].open(output_file + "_ch_" + std::to_string(i) + ".fc32", std::ios::binary);
      if (!outfiles[i].is_open()) {
        fprintf(stderr, "Failed to open output file\n");
        exit(EXIT_FAILURE);
      }
    }
  } else {
    outfiles[0].open(output_file + ".fc32", std::ios::binary);
    if (!outfiles[0].is_open()) {
      fprintf(stderr, "Failed to open output file\n");
      exit(EXIT_FAILURE);
    }
  }

  // Create resampler
  msresamp_crcf resampler = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);
  while (!stop_flag.load()) {
    std::shared_ptr<frame_t> frame = nullptr;
    {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [] { return !queue.empty() || stop_flag.load(); });
      if (stop_flag.load()) {
        break;
      }
      frame = queue.front();
      queue.pop();
    }
    for (uint32_t i = 0; i < num_channels; i++) {
      uint32_t num_output_samples = frame->buffer_size;
      if (enable_resampler) {
        msresamp_crcf_execute(resampler,
                              (liquid_float_complex*)frame->buffer[i]->data(),
                              sf_len_sdr,
                              (liquid_float_complex*)frame->buffer[i]->data(),
                              &num_output_samples);
      }
      outfiles[i].write((char*)frame->buffer[i]->data(), num_output_samples * sizeof(cf_t));
    }
    if (frame->frames_idx % 100 == 0) {
      printf(".");
      fflush(stdout);
    }
  }
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  std::signal(SIGINT, sigint_handler);
  pthread_t receiver_thread, writer_thread;
  // Create receiver thread
  if (pthread_create(&receiver_thread, nullptr, (void* (*)(void*))receiver_worker, nullptr) != 0) {
    fprintf(stderr, "Failed to create receiver thread\n");
    return -1;
  }
  // Set thread to the highest priority
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
  return 0;
}