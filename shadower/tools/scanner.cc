#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
#include "srsran/common/band_helper.h"
#include "srsran/phy/sync/ssb.h"
uint16_t                    band             = 78;
double                      srate            = 23.04e6;
double                      center_frequency = 3427.5e6;
double                      start_frequency  = 3417.5e6;
double                      stop_frequency   = 3437.5e6;
uint32_t                    sf_len           = srate / 1000;
srsran_ssb_pattern_t        pattern          = SRSRAN_SSB_PATTERN_C;
srsran_duplex_mode_t        duplex_mode      = SRSRAN_DUPLEX_MODE_TDD;
srsran_subcarrier_spacing_t scs              = srsran_subcarrier_spacing_30kHz;
Source*                     source           = nullptr;
srsran_timestamp_t          ts               = {};
srsran_ssb_t                ssb              = {};
srsran_ssb_args_t           ssb_args         = {};
srsran_ssb_cfg_t            ssb_cfg          = {};
cf_t*                       buffer           = nullptr;
uint32_t                    prbs             = 51;

void scan_ssb(double ssb_freq, srslog::basic_logger& logger)
{
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
  ssb_cfg.center_freq_hz = center_frequency;
  ssb_cfg.ssb_freq_hz    = ssb_freq;
  ssb_cfg.scs            = scs;
  ssb_cfg.pattern        = pattern;
  ssb_cfg.duplex_mode    = duplex_mode;
  ssb_cfg.periodicity_ms = 10;
  if (srsran_ssb_set_cfg(&ssb, &ssb_cfg) < SRSRAN_SUCCESS) {
    logger.error("Error setting SSB configuration");
    goto cleanup;
  }

  for (uint32_t i = 0; i < 100; i++) {
    /* Receive samples */
    source->receive(buffer, sf_len * 0.1, &ts);
    source->receive(buffer, sf_len, &ts);
    /* search for SSB */
    srsran_ssb_search_res_t res = {};
    if (srsran_ssb_search(&ssb, buffer, sf_len, &res) < SRSRAN_SUCCESS) {
      logger.error("Error running srsran_ssb_search");
      goto cleanup;
    }
    /* If snr too small then continue */
    if (res.measurements.snr_dB < -10.0f || !res.pbch_msg.crc) {
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

    double scs_hz          = 15e3 * (1 << scs);
    double frequncy_pointA = ssb_freq - mib.ssb_offset * 12 * scs_hz - 120 * scs_hz;
    logger.info("Frequency point A: %f MHz", frequncy_pointA / 1e6);

    goto cleanup;
    return;
  }
cleanup:
  srsran_ssb_free(&ssb);
}

void scan_center(double center_freq, srslog::basic_logger& logger, double bw)
{
  /* Initialize source */
  ShadowerConfig config = {};
  config.sample_rate    = srate;
  config.dl_freq        = center_frequency;
  config.ul_freq        = center_frequency;
  config.rx_gain        = 40;
  config.tx_gain        = 80;

  config.source_params               = "type=b200";
  create_source_t uhd_source_creator = load_source(uhd_source_module_path);
  source                             = uhd_source_creator(config);

  srsran::srsran_band_helper                band_helper;
  srsran::srsran_band_helper::sync_raster_t sync_raster = band_helper.get_sync_raster(band, scs);
  if (!sync_raster.valid()) {
    logger.error("Invalid band %d or SCS %d kHz\n", band, scs);
    return;
  }

  /* Initialize the buffer */
  buffer = srsran_vec_cf_malloc(sf_len * 2);
  if (!buffer) {
    logger.error("Failed to allocate buffer");
    return;
  }

  double ssb_lower = center_freq - bw;
  double ssb_upper = center_freq + bw;

  /* Enumerate all possible SSB frequencies */
  while (!sync_raster.end()) {
    double ssb_freq = sync_raster.get_frequency();
    if (ssb_freq < ssb_lower) {
      sync_raster.next();
      continue;
    }
    if (ssb_freq > ssb_upper) {
      break;
    }
    logger.info("Scanning SSB at %.2f MHz", ssb_freq / 1e6);
    scan_ssb(ssb_freq, logger);
    sync_raster.next();
  }

  source->close();
  free(buffer);
  logger.info("Finished scanning the cells around the center frequency %f MHz", center_freq / 1e6);
}

int main(int argc, char* argv[])
{
  /* Initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::info);

  /* Retrieve parameters from command line */
  if (argc < 3) {
    printf("Usage: %s <band> <scs>\n", argv[0]);
    return 1;
  }
  band             = atoi(argv[1]);
  uint32_t scs_kHz = atoi(argv[2]);
  scs              = static_cast<srsran_subcarrier_spacing_t>((scs_kHz / 15) >> 1);

  /* If center frequency config is provided */
  if (argc > 3) {
    double centerFreqMHz = atof(argv[3]);
    center_frequency     = centerFreqMHz * 1e6;
  }

  /* If start and stop frequency config is provided */
  if (argc > 4) {
    double startFreqMHz = atof(argv[3]);
    start_frequency     = startFreqMHz * 1e6;

    double stopFreqMHz = atof(argv[4]);
    stop_frequency     = stopFreqMHz * 1e6;
  }

  double bw_step   = 9e6;
  center_frequency = start_frequency + bw_step;
  while (center_frequency < stop_frequency) {
    scan_center(center_frequency, logger, bw_step);
    center_frequency += (bw_step * 2);
  }
}
