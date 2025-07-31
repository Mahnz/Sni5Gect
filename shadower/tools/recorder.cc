#include "shadower/source/source.h"
#include "shadower/utils/arg_parser.h"
#include "shadower/utils/buffer_pool.h"
#include "shadower/utils/constants.h"
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

bool           fdd           = false;
bool           ul_freq_set   = false;
double         dl_freq       = 3427.5e6;
double         ul_freq       = 3427.5e6;
double         sample_rate   = 23.04e6;
double         gain          = 40;
uint32_t       num_frames    = 20000;
uint32_t       num_channels  = 1;
std::string    output_file   = "output";
std::string    output_folder = "/root/records/";
std::string    source_type   = "uhd";
std::string    device_args   = "type=b200";
double         resample_rate = 1.0;
ShadowerConfig config        = {};
uint32_t       sf_len        = 0;

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
      {"ul-freq", required_argument, 0, 'u'},
      {"srate", required_argument, 0, 's'},
      {"gain", required_argument, 0, 'g'},
      {"frames", required_argument, 0, 'n'},
      {"channels", required_argument, 0, 'c'},
      {"output", required_argument, 0, 'o'},
      {"folder", required_argument, 0, 'F'},
      {"source-type", required_argument, 0, 't'},
      {"device-args", required_argument, 0, 'd'},
      {0, 0, 0, 0}

  };

  while ((opt = getopt_long(argc, argv, "f:u:s:g:n:c:o:F:t:d:h", long_options, &option_index)) != -1) {
    switch (opt) {
      case 'f': {
        double dl_freq_MHz = atof(optarg);
        dl_freq            = dl_freq_MHz * 1e6;
        break;
      }
      case 'u': {
        double ul_freq_MHz = atof(optarg);
        ul_freq            = ul_freq_MHz * 1e6;
        ul_freq_set        = true;
        break;
      }
      case 's': {
        double sample_rate_MHz = atof(optarg);
        sample_rate            = sample_rate_MHz * 1e6;
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
      case 'h': {
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("  -f, --frequency <MHz>       Set the downlink frequency (MHz)\n");
        printf("  -u, --ul-freq <MHz>         Set the uplink frequency (MHz)\n");
        printf("  -s, --srate <MHz>           Set the sample rate (MHz)\n");
        printf("  -g, --gain <dB>             Set the gain (dB)\n");
        printf("  -n, --frames <count>        Set the number of frames to process\n");
        printf("  -c, --channels <count>      Set the number of channels\n");
        printf("  -o, --output <file>         Set the output file name\n");
        printf("  -F, --folder <path>         Set the output folder path\n");
        printf("  -t, --source-type <type>    Set the source type (e.g., uhd)\n");
        printf("  -d, --device-args <args>    Set the device arguments for the source\n");
        printf("  -h, --help                  Show this help message\n");
        exit(EXIT_SUCCESS);
      }
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }

  /* if dl frequency is different from ul frequency, then fdd is set to true */
  if (ul_freq_set && ul_freq != dl_freq) {
    fdd = true;
  } else {
    ul_freq = dl_freq;
  }

  printf("Using DL Center Frequency: %f MHz\n", dl_freq / 1e6);
  printf("      UL Center Frequency: %f MHz\n", ul_freq / 1e6);
  printf("      Sample Rate: %f MHz\n", sample_rate / 1e6);
  printf("      Gain: %f db\n", gain);
  printf("      Number of Channels: %d\n", num_channels);
  printf("      Device Args: %s\n", device_args.c_str());
  printf("      Is FDD: %s\n", fdd ? "true" : "false");
  output_file = output_folder + output_file;
  if (num_channels == 1) {
    printf("      Output File: %s\n", output_file.c_str());
  }
  config.sample_rate   = sample_rate;
  config.dl_freq       = dl_freq;
  config.ul_freq       = ul_freq;
  config.rx_gain       = gain;
  config.tx_gain       = gain;
  config.nof_channels  = num_channels;
  config.source_params = device_args;
  sf_len               = sample_rate * SF_DURATION;
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

  buffer_pool              = new SharedBufferPool(sf_len, 100);
  srsran_timestamp_t ts    = {};
  uint32_t           count = 0;
  while (count++ < num_frames) {
    std::shared_ptr<frame_t> frame = std::make_shared<frame_t>();
    cf_t*                    rx_buffer[SRSRAN_MAX_CHANNELS];
    frame->frames_idx = count;

    for (uint32_t i = 0; i < num_channels; i++) {
      std::shared_ptr<std::vector<cf_t> > buf = buffer_pool->get_buffer();
      frame->buffer[i]                        = buf;
      rx_buffer[i]                            = buf->data();
    }

    int result = source->recv(rx_buffer, sf_len, &ts);
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