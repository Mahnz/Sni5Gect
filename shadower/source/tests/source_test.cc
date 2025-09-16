#include "shadower/source/source.h"
#include "shadower/utils/constants.h"
#include "shadower/utils/utils.h"
#include <algorithm>
#include <getopt.h>

ShadowerConfig config        = {};
float          send_delay    = 2e-3;
uint32_t       rounds        = 1000;
std::string    source_type   = "uhd";
std::string    source_params = "type=b200";
std::string    sample_file   = "shadower/test/data/ssb.fc32";

void usage(const char* prog)
{
  printf("Usage: %s [options]\n", prog);
  printf("  -f <freq[,freq]>  DL,UL freq for channel 0 in MHz (if one value given, both RX/TX use it)\n");
  printf("  -F <freq[,freq]>  DL,UL freq for channel 1 in MHz\n");
  printf("  -g <rx,tx>        RX,TX gains for channel 0\n");
  printf("  -G <rx,tx>        RX,TX gains for channel 1\n");
  printf("  -s <MHz>          Sample rate in MHz\n");
  printf("  -t <str>          Source type\n");
  printf("  -d <str>          Source parameters\n");
  printf("  -r <n>            Number of test rounds\n");
}

static void parse_freq(const char* arg, double& rx, double& tx)
{
  double f1 = 0, f2 = 0;
  int    n = sscanf(arg, "%lf,%lf", &f1, &f2);
  if (n == 1) {
    rx = tx = f1 * 1e6;
  } else if (n == 2) {
    rx = f1 * 1e6;
    tx = f2 * 1e6;
  } else {
    fprintf(stderr, "Invalid frequency format: %s\n", arg);
    exit(EXIT_FAILURE);
  }
}

static void parse_gain(const char* arg, double& rx, double& tx)
{
  double g1 = 0, g2 = 0;
  if (sscanf(arg, "%lf,%lf", &g1, &g2) != 2) {
    fprintf(stderr, "Invalid gain format (expected rx,tx): %s\n", arg);
    exit(EXIT_FAILURE);
  }
  rx = g1;
  tx = g2;
}

void parse_args(int argc, char** argv)
{
  int opt;
  config.channels.resize(2);
  while ((opt = getopt(argc, argv, "f:F:g:G:s:t:d:r:")) != -1) {
    switch (opt) {
      case 'f': {
        parse_freq(optarg, config.channels[0].rx_frequency, config.channels[0].tx_frequency);
        config.channels[0].enabled = true;
        break;
      }
      case 'F': {
        parse_freq(optarg, config.channels[1].rx_frequency, config.channels[1].tx_frequency);
        config.channels[1].enabled = true;
        break;
      }
      case 'g': {
        parse_gain(optarg, config.channels[0].rx_gain, config.channels[0].tx_gain);
        break;
      }
      case 'G': {
        parse_gain(optarg, config.channels[1].rx_gain, config.channels[1].tx_gain);
        break;
      }
      case 's':
        config.sample_rate = strtod(optarg, nullptr) * 1e6;
        break;
      case 't':
        config.source_type = optarg;
        break;
      case 'd':
        config.source_params = optarg;
        break;

      case 'r':
        rounds = atoi(optarg);
        break;
      default:
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
  }
  config.nof_channels = 0;
  for (uint32_t i = 0; i < 2; i++) {
    if (config.channels[i].enabled) {
      config.nof_channels++;
    }
  }
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  /* initialize logger */
  config.log_level             = srslog::basic_levels::debug;
  srslog::basic_logger& logger = srslog_init(&config);
  uint32_t              sf_len = config.sample_rate * SF_DURATION;

  if (config.source_type == "uhd") {
    config.source_module = uhd_source_module_path;
  } else if (config.source_type == "file") {
    config.source_module = file_source_module_path;
  } else {
    logger.error("Unknown source type %s\n", config.source_type.c_str());
    return -1;
  }
  /* Load the test IQ samples from file */
  cf_t* test_buffer = srsran_vec_cf_malloc(sf_len);
  if (!load_samples(sample_file, test_buffer, sf_len)) {
    logger.error("Error loading samples\n");
    return -1;
  }

  char               filename[64];
  create_source_t    creator = load_source(config.source_module);
  Source*            source  = creator(config);
  cf_t*              rx_buffers[SRSRAN_MAX_CHANNELS];
  cf_t*              tx_buffers[SRSRAN_MAX_CHANNELS];
  srsran_timestamp_t ts = {};
  for (uint32_t i = 0; i < config.nof_channels; i++) {
    rx_buffers[i] = srsran_vec_cf_malloc(sf_len);
    tx_buffers[i] = test_buffer;
  }

  for (uint32_t i = 0; i < rounds; i++) {
    /* Receive the samples again */
    source->recv(rx_buffers, sf_len, &ts);
    if (i % 5 == 0) {
      /* Send the samples out */
      srsran_timestamp_add(&ts, 0, send_delay);
      source->send(tx_buffers, sf_len, ts);
    }
    for (uint32_t ch = 0; ch < config.nof_channels; ch++) {
      sprintf(filename, "received_data_%u_ch_%u", i, ch);
      write_record_to_file(rx_buffers[ch], sf_len, filename);
    }
    if (i % 10 == 0) {
      printf(".");
      fflush(stdout);
    }
  }
  source->close();
  for (uint32_t i = 0; i < config.nof_channels; i++) {
    free(rx_buffers[i]);
  }
  free(test_buffer);
}