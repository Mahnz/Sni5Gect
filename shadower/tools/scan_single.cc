#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
#include "srsran/common/band_helper.h"
#include "srsran/phy/sync/ssb.h"
#include <liquid/liquid.h>

srsran_ssb_pattern_t        pattern     = SRSRAN_SSB_PATTERN_C;
srsran_duplex_mode_t        duplex_mode = SRSRAN_DUPLEX_MODE_TDD;
srsran_subcarrier_spacing_t scs         = srsran_subcarrier_spacing_30kHz;

void scan_ssb(Source*               source,
              double                srate,
              double                sourceSrate,
              double                center_freq,
              double                ssb_freq,
              srslog::basic_logger& logger,
              bool                  enable_resampler,
              msresamp_crcf         resampler,
              uint32_t              round = 100)
{
  srsran_ssb_t       ssb      = {};
  srsran_ssb_args_t  ssb_args = {};
  srsran_ssb_cfg_t   ssb_cfg  = {};
  srsran_timestamp_t ts       = {};
  double             cfo      = 600.0f;

  uint32_t sf_len = srate * SF_DURATION;
  if (enable_resampler) {
    sf_len = sourceSrate * SF_DURATION;
  }
  cf_t* buffer     = srsran_vec_cf_malloc(sf_len);
  cf_t* tmp_buffer = srsran_vec_cf_malloc(sf_len);

  /* Initialize ssb */
  ssb_args.max_srate_hz   = srate;
  ssb_args.min_scs        = scs;
  ssb_args.enable_search  = true;
  ssb_args.enable_measure = true;
  ssb_args.enable_decode  = true;
  if (srsran_ssb_init(&ssb, &ssb_args) != 0) {
    logger.error("Failed to initialize SSB");
    goto cleanup;
  }

  /* Initialize ssb */
  ssb_cfg.srate_hz       = srate;
  ssb_cfg.center_freq_hz = center_freq;
  ssb_cfg.ssb_freq_hz    = ssb_freq;
  ssb_cfg.scs            = scs;
  ssb_cfg.pattern        = pattern;
  ssb_cfg.duplex_mode    = duplex_mode;
  ssb_cfg.periodicity_ms = 10;
  if (srsran_ssb_set_cfg(&ssb, &ssb_cfg) < SRSRAN_SUCCESS) {
    logger.error("Error setting SSB configuration");
    goto cleanup;
  }

  for (uint32_t i = 0; i < round; i++) {
    char filename[64];
    sprintf(filename, "s_%u", i);

    uint32_t num_samples = sf_len;
    /* Receive samples */
    if (enable_resampler) {
      source->receive(tmp_buffer, sf_len * 0.1, &ts);
      source->receive(tmp_buffer, sf_len, &ts);
      uint32_t num_output;
      msresamp_crcf_execute(
          resampler, (liquid_float_complex*)tmp_buffer, sf_len, (liquid_float_complex*)buffer, &num_output);
      num_samples = num_output;
    } else {
      // source->receive(buffer, sf_len * 0.1, &ts);
      source->receive(buffer, sf_len, &ts);
      num_samples = sf_len;
    }
    auto start = std::chrono::high_resolution_clock::now();
    // srsran_vec_apply_cfo(buffer, -cfo / srate, buffer, num_samples);
    // write_record_to_file(buffer, num_samples, filename);
    /* search for SSB */
    srsran_ssb_search_res_t res = {};
    if (srsran_ssb_search(&ssb, buffer, num_samples, &res) < SRSRAN_SUCCESS) {
      logger.error("Error running srsran_ssb_search");
      goto cleanup;
    }
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    /* If snr too small then continue */
    if (!res.pbch_msg.crc) {
      continue;
    }
    /* Decode MIB */
    srsran_mib_nr_t mib = {};
    if (srsran_pbch_msg_nr_mib_unpack(&res.pbch_msg, &mib) < SRSRAN_SUCCESS) {
      logger.error("Error running srsran_pbch_msg_nr_mib_unpack");
      continue;
    }
    /* Print cell info */
    std::array<char, 512> mib_info_str = {};
    srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), mib_info_str.size());
    logger.info("Found cell: cellid: %u %s", res.N_id, mib_info_str.data());

    srsran_csi_trs_measurements_t& measure = res.measurements;
    logger.info("SNR: %f dB", measure.snr_dB);
    logger.info("CFO: %f Hz", measure.cfo_hz);
    logger.info("ssb idx: %u", res.pbch_msg.hrf);

    sprintf(filename, "ssb_%u_%f", i, ssb_freq);
    write_record_to_file(buffer, sf_len, filename);
  }
cleanup:
  srsran_ssb_free(&ssb);
  free(buffer);
}

int main(int argc, char* argv[])
{
  /* Initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::info);

  /* Retrieve parameters from command line */
  if (argc < 3) {
    printf("Usage: %s <center_freq> <ssb_freq>\n", argv[0]);
    return 1;
  }

  double center_frequency = atof(argv[1]) * 1e6;
  double ssb_freq         = atof(argv[2]) * 1e6;

  std::string sdr_param = "type=b200";
  if (argc > 3) {
    sdr_param = argv[3];
  }

  double srate = 46.08e6;
  if (argc > 4) {
    double srateMHz = atof(argv[4]);
    srate           = srateMHz * 1e6;
  }

  double sourceSrate      = srate;
  bool   enable_resampler = false;
  if (argc > 5) {
    sourceSrate      = atof(argv[5]) * 1e6;
    enable_resampler = true;
  }

  float         resample_rate = srate / sourceSrate;
  msresamp_crcf resampler     = msresamp_crcf_create(resample_rate, TARGET_STOPBAND_SUPPRESSION);

  logger.info("Scanning SSB at %f MHz Center: %f\n", ssb_freq / 1e6, center_frequency / 1e6);
  /* Initialize source */
  ShadowerConfig config = {};
  config.sample_rate    = sourceSrate;
  config.dl_freq        = center_frequency;
  config.ul_freq        = center_frequency;
  config.rx_gain        = 40;
  config.tx_gain        = 80;

  config.source_params = sdr_param;
  logger.info("Using source params: %s", config.source_params.c_str());
  create_source_t uhd_source_creator = load_source(uhd_source_module_path);
  Source*         source             = uhd_source_creator(config);

  logger.info("Scanning SSB at %f MHz Center: %f", ssb_freq / 1e6, center_frequency / 1e6);
  scan_ssb(source, srate, sourceSrate, center_frequency, ssb_freq, logger, enable_resampler, resampler, 10000);
  msresamp_crcf_destroy(resampler);
  source->close();
}
