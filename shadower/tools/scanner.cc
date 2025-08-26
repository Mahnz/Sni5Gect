#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
#include "srsran/common/band_helper.h"
#include "srsran/phy/sync/ssb.h"
uint16_t                    band             = 78;
double                      srate            = 23.04e6;
double                      center_frequency = 3427.5e6;
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

  for (uint32_t i = 0; i < 1000; i++) {
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

    goto cleanup;
    return;
  }
cleanup:
  srsran_ssb_free(&ssb);
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

  if (argc > 4) {
    double srateMHz = atof(argv[4]);
    srate           = srateMHz * 1e6;
  }

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

  // create_source_t limesdr_source = load_source(limesdr_source_module_path);
  // config.source_params =
  //     "logLevel:3,port0:\"dev0\",dev0:\"XTRX\",dev0_chipIndex:0,"
  //     "dev0_linkFormat:\"I12\",dev0_rx_path:\"LNAH\",dev0_tx_path:\"Band1\","
  //     "dev0_max_channels_to_use:1,dev0_calibration:\"none\",dev0_rx_gfir_enable:0,dev0_tx_gfir_enable:0";
  // config.rx_gain = 50;
  // source         = limesdr_source(config);

  /* If sample rate config is provided */
  if (argc > 4) {
    double srateMHz = atof(argv[4]);
    srate           = srateMHz * 1e6;
    sf_len          = srate / 1000;
  }

  /* Retrieve the sync raster config */
  srsran::srsran_band_helper                band_helper;
  srsran::srsran_band_helper::sync_raster_t sync_raster = band_helper.get_sync_raster(band, scs);
  if (!sync_raster.valid()) {
    printf("Invalid band %d or SCS %d kHz\n", band, scs_kHz);
    return 1;
  }

  /* Initialize the buffer */
  buffer = srsran_vec_cf_malloc(sf_len * 2);
  if (!buffer) {
    logger.error("Failed to allocate buffer");
  }

  int symbol_sz    = srsran_symbol_sz_from_srate(srate, scs);
  prbs             = srsran_nof_prb(symbol_sz);
  double bandwidth = prbs * 12 * (15000 << scs);
  logger.info("Bandwidth: %f MHz", bandwidth / 1000);

  /* Configure the SSB search frequency lower limit and upper limit */
  double ssb_lower = center_frequency - bandwidth / 2;
  double ssb_upper = center_frequency + bandwidth / 2;

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

  free(buffer);
  source->close();
}
