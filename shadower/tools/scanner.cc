#include "shadower/hdr/source.h"
#include "shadower/hdr/utils.h"
#include "srsran/common/band_helper.h"
#include "srsran/phy/sync/ssb.h"

void scan_ssb(Source*                     source,
              double                      srate,
              double                      center_freq,
              double                      ssb_freq,
              srslog::basic_logger&       logger,
              srsran_subcarrier_spacing_t scs,
              uint32_t                    round = 100)
{
  srsran_ssb_t         ssb         = {};
  srsran_ssb_args_t    ssb_args    = {};
  srsran_ssb_cfg_t     ssb_cfg     = {};
  srsran_timestamp_t   ts          = {};
  srsran_ssb_pattern_t pattern     = SRSRAN_SSB_PATTERN_C;
  srsran_duplex_mode_t duplex_mode = SRSRAN_DUPLEX_MODE_TDD;
  uint32_t             sf_len      = srate * SF_DURATION;
  cf_t*                buffer      = srsran_vec_cf_malloc(sf_len);

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

    char filename[64];
    sprintf(filename, "ssb_%u_%f", res.t_offset, ssb_freq);
    std::string output_folder = "/root/records/";
    write_record_to_file(buffer, sf_len, filename, output_folder);
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
  if (argc < 2) {
    printf("Usage: %s <band> <center frequency> <sample rate>\n", argv[0]);
    return 1;
  }
  band             = atoi(argv[1]);

  /* If center frequency config is provided */
  if (argc > 2) {
    double centerFreqMHz = atof(argv[2]);
    center_frequency     = centerFreqMHz * 1e6;
  }

  if (argc > 3) {
    double srateMHz = atof(argv[3]);
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
  Source*         source             = uhd_source_creator(config);

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
    printf("Can not get the sync raster for band %u\n", band);
    return 1;
  }

  while (!sync_raster.end()) {
    double ssb_freq = sync_raster.get_frequency();
    if (ssb_freq < center_freq - 10e6) {
      sync_raster.next();
      continue;
    }
    if (ssb_freq > center_freq + 10e6) {
      break;
    }

    logger.info("Scanning SSB at %.2f MHz", ssb_freq / 1e6);
    scan_ssb(source, srate, center_freq, ssb_freq, logger, scs, 1000);
    sync_raster.next();
  }
  source->close();
}
