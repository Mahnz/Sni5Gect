#include "shadower/source/source.h"
#include "shadower/utils/constants.h"
#include "shadower/utils/utils.h"
#include <getopt.h>

ShadowerConfig config        = {};
float          send_delay    = 2e-3;
uint32_t       rounds        = 1000;
std::string    source_type   = "uhd";
std::string    source_params = "type=b200";
std::string    sample_file   = "shadower/test/data/ssb.fc32";

void parse_args(int argc, char** argv)
{
  int opt;
  while ((opt = getopt(argc, argv, "fgstdcr")) != -1) {
    switch (opt) {
      case 'f': {
        double centerFreqMHz = atof(argv[optind]);
        config.dl_freq       = centerFreqMHz * 1e6;
        config.ul_freq       = centerFreqMHz * 1e6;
        printf("Using center frequency %f\n", config.dl_freq);
        break;
      }
      case 'g':
        config.tx_gain = atoi(argv[optind]) + 20;
        config.rx_gain = atoi(argv[optind]);
        break;
      case 's': {
        double sampleRateMHz = atof(argv[optind]);
        config.sample_rate   = sampleRateMHz * 1e6;
        config.source_srate  = sampleRateMHz * 1e6;
        break;
      }
      case 't':
        config.source_type = argv[optind];
        break;
      case 'd':
        config.source_params = argv[optind];
        break;
      case 'c':
        config.nof_channels = atoi(argv[optind]);
        break;
      case 'r':
        rounds = atoi(argv[optind]);
        break;
      default:
        fprintf(stderr, "Unknown option %s\n", argv[optind]);
        exit(EXIT_FAILURE);
    }
  }
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  /* initialize logger */
  config.log_level               = srslog::basic_levels::debug;
  srslog::basic_logger& logger   = srslog_init(&config);
  uint32_t              sf_len   = config.sample_rate * SF_DURATION;
  uint32_t              slot_len = sf_len / 2;

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
    sprintf(filename, "received_data_%u", i);
    /* Receive the samples again */
    source->recv(rx_buffers, sf_len, &ts);
    if (i % 5 == 0) {
      /* Send the samples out */
      srsran_timestamp_add(&ts, 0, send_delay);
      source->send(tx_buffers, slot_len, ts);
    }
    write_record_to_file(rx_buffers[0], sf_len, filename);
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