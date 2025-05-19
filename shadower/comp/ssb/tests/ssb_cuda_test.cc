#include "shadower/comp/ssb/ssb_cuda.cuh"
#include "shadower/utils/arg_parser.h"
#include "shadower/utils/constants.h"
#include "shadower/utils/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include <getopt.h>

ShadowerConfig config = {};
std::string    sample_file;
uint32_t       cell_id = 1;
uint32_t       rounds  = 1000;

void parse_args(int argc, char* argv[])
{
  int opt;
  while ((opt = getopt(argc, argv, "sfbFIr")) != -1) {
    switch (opt) {
      case 's': {
        double srateMHz    = atof(argv[optind]);
        config.sample_rate = srateMHz * 1e6;
        printf("Using sample rate: %f MHz\n", srateMHz);
        break;
      }
      case 'f': {
        double freqMHz = atof(argv[optind]);
        config.dl_freq = freqMHz * 1e6;
        config.ul_freq = config.dl_freq;
        printf("Using center frequency: %f MHz\n", config.dl_freq);
        break;
      }
      case 'b': {
        double ssbFreqMHz = atof(argv[optind]);
        config.ssb_freq   = ssbFreqMHz * 1e6;
        printf("Using SSB Frequency: %f MHz\n", config.ssb_freq);
        break;
      }
      case 'F': {
        sample_file = argv[optind];
        printf("Using sample file: %s\n", sample_file.c_str());
        break;
      }
      case 'I': {
        cell_id = atoi(argv[optind]);
        printf("Using cell id: %u\n", cell_id);
        break;
      }
      case 'r': {
        rounds = atoi(argv[optind]);
        printf("Round each test for: %u rounds\n", rounds);
        break;
      }
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }
  config.ssb_pattern = srsran_ssb_pattern_t::SRSRAN_SSB_PATTERN_C;
  config.duplex_mode = srsran_duplex_mode_t::SRSRAN_DUPLEX_MODE_TDD;
  config.scs_ssb     = srsran_subcarrier_spacing_t::srsran_subcarrier_spacing_30kHz;
  if (sample_file.empty()) {
    fprintf(stderr, "Sample file is required.\n");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  /* initialize logger */
  config.log_level             = srslog::basic_levels::debug;
  srslog::basic_logger& logger = srslog_init(&config);
  uint32_t              sf_len = config.sample_rate * SF_DURATION;

  /* load IQ samples from file */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  /* initialize ssb */
  srsran_ssb_t ssb = {};
  if (!init_ssb(ssb,
                config.sample_rate,
                config.dl_freq,
                config.ssb_freq,
                config.scs_ssb,
                config.ssb_pattern,
                config.duplex_mode)) {
    logger.error("Failed to initialize SSB");
    return -1;
  }

  srsran_csi_trs_measurements_t measurement = {};
  srsran_pbch_msg_nr_t          pbch_msg    = {};
  srsran_ssb_search_res_t       res         = {};

  /* Measure SSB find time */
  auto t_start_ssb_find = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < rounds; i++) {
    if (srsran_ssb_find(&ssb, samples.data(), cell_id, &measurement, &pbch_msg) != SRSRAN_SUCCESS) {
      logger.error("Error running srsran_ssb_find");
      return -1;
    }
  }
  auto t_end_ssb_find = std::chrono::high_resolution_clock::now();
  logger.info("srsran_ssb_find: %ld us",
              std::chrono::duration_cast<std::chrono::microseconds>(t_end_ssb_find - t_start_ssb_find).count() /
                  rounds);
  if (!pbch_msg.crc) {
    logger.error("PBCH CRC not match (srsran_ssb_find)");
  }

  /* Measure SSB track time */
  auto t_start_ssb_track = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < rounds; i++) {
    if (srsran_ssb_track(&ssb, samples.data(), cell_id, 0, 0, &measurement, &pbch_msg) != SRSRAN_SUCCESS) {
      logger.error("Error running srsran_ssb_track");
      return -1;
    }
  }
  auto t_end_ssb_track = std::chrono::high_resolution_clock::now();
  logger.info("srsran_ssb_track: %ld us",
              std::chrono::duration_cast<std::chrono::microseconds>(t_end_ssb_track - t_start_ssb_track).count() /
                  rounds);

/* SSB cuda run sync find time */
#if ENABLE_CUDA
  SSBCuda ssb_cuda(
      config.sample_rate, config.dl_freq, config.ssb_freq, config.scs_ssb, config.ssb_pattern, config.duplex_mode);
  if (!ssb_cuda.init(SRSRAN_NID_2_NR(cell_id))) {
    logger.error("Failed to initialize SSB CUDA");
    return -1;
  }
  auto t_start_cuda = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < rounds; i++) {
    ssb_cuda.ssb_run_sync_find(samples.data(), cell_id, &measurement, &pbch_msg);
  }
  auto t_end_cuda = std::chrono::high_resolution_clock::now();
  logger.info("ssb_run_sync_find: %ld us",
              std::chrono::duration_cast<std::chrono::microseconds>(t_end_cuda - t_start_cuda).count() / rounds);
  if (!pbch_msg.crc) {
    logger.error("ssb_cuda.ssb_run_sync_find PBCH CRC not match");
    return -1;
  }
  ssb_cuda.cleanup();
#endif // ENABLE_CUDA
  return 0;
}