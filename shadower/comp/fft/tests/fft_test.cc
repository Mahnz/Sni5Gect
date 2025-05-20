#if ENABLE_CUDA
#include "shadower/comp/fft/fft_processor.cuh"
#endif // ENABLE_CUDA
#include "shadower/utils/arg_parser.h"
#include "shadower/utils/constants.h"
#include "shadower/utils/phy_cfg_utils.h"
#include "shadower/utils/ue_dl_utils.h"
#include "shadower/utils/utils.h"
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <vector>

ShadowerConfig config = {};
std::string    sample_file;
uint32_t       cell_id = 1;
uint32_t       half    = 1;
uint32_t       rounds  = 1000;

void parse_args(int argc, char* argv[])
{
  int opt;
  while ((opt = getopt(argc, argv, "sfbFIrph")) != -1) {
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
      case 'p': {
        config.nof_prb = atoi(argv[optind]);
        printf("Number of prbs: %u\n", config.nof_prb);
        break;
      }
      case 'h': {
        half = atoi(argv[optind]);
        printf("Half: %u\n", half);
        break;
      }
      default:
        fprintf(stderr, "Unknown option or missing argument.\n");
        exit(EXIT_FAILURE);
    }
  }
  config.ssb_pattern = srsran_ssb_pattern_t::SRSRAN_SSB_PATTERN_C;
  config.duplex_mode = srsran_duplex_mode_t::SRSRAN_DUPLEX_MODE_TDD;
  config.scs_common  = srsran_subcarrier_spacing_t::srsran_subcarrier_spacing_30kHz;
  config.scs_ssb     = srsran_subcarrier_spacing_t::srsran_subcarrier_spacing_30kHz;
  if (sample_file.empty()) {
    fprintf(stderr, "Sample file is required.\n");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  srslog::basic_logger& logger = srslog_init(&config);
  logger.set_level(srslog::basic_levels::debug);

  /* initialize phy cfg */
  srsran::phy_cfg_nr_t phy_cfg = {};
  init_phy_cfg(phy_cfg, config);
  srsran_slot_cfg_t slot_cfg = {.idx = 1};

  /* UE DL init with configuration from phy_cfg */
  uint32_t          sf_len   = config.sample_rate * SF_DURATION;
  uint32_t          slot_len = sf_len / (1 << (uint32_t)config.scs_common);
  srsran_ue_dl_nr_t ue_dl    = {};
  cf_t*             buffer   = srsran_vec_cf_malloc(sf_len);
  if (!init_ue_dl(ue_dl, buffer, phy_cfg)) {
    logger.error("Failed to init UE DL");
    return -1;
  }

  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }
  std::cout << "Loaded samples from " << sample_file << std::endl;
  srsran_vec_cf_copy(buffer, samples.data() + half * slot_len, slot_len);

  /* CPU processing of samples */
  auto start_cpu = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < rounds; i++) {
    srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);
  }
  auto end_cpu = std::chrono::high_resolution_clock::now();

  auto elapsed_seconds_cpu =
      std::chrono::duration_cast<std::chrono::microseconds>(end_cpu - start_cpu).count() / rounds;
  std::cout << "CPU time: " << elapsed_seconds_cpu << " micro seconds\n";

#if ENABLE_CUDA
  // Initialize FFT Processor
  FFTProcessor fft_processor(config.sample_rate, config.dl_freq, config.scs_common, &ue_dl.fft[0]);
  cf_t*        output_ofdm_symbols = srsran_vec_cf_malloc(fft_processor.fft_size * 14);

  /* GPU processing of samples */
  auto start_gpu = std::chrono::high_resolution_clock::now();
  for (uint32_t i = 0; i < rounds; i++) {
    fft_processor.to_ofdm(samples.data() + half * slot_len, output_ofdm_symbols, 1);
  }
  auto end_gpu = std::chrono::high_resolution_clock::now();
  auto elapsed_seconds_gpu =
      std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu).count() / rounds;
  std::cout << "GPU time: " << elapsed_seconds_gpu << " micro seconds\n";

  char filename[64];
  sprintf(filename, "raw");
  write_record_to_file(samples.data() + half * slot_len, slot_len, filename);

  sprintf(filename, "ofdm_output_fft%u", fft_processor.nof_re);
  write_record_to_file(output_ofdm_symbols, fft_processor.nof_re * 14, filename);
#endif // ENABLE_CUDA
  return 0;
}
