#include "shadower/source/source.h"
#include "shadower/utils/utils.h"
#include "srsran/common/band_helper.h"
#include "srsran/phy/sync/ssb.h"
#include <getopt.h>

ShadowerConfig              config      = {};
double                      sample_rate = 23.04e6;
uint32_t                    sf_len      = 23.04e3;
double                      start_freq;
double                      stop_freq;
uint32_t                    band          = 78;
uint32_t                    rounds        = 100;
uint32_t                    rx_gain       = 40;
std::string                 source_params = "type=b200";
srsran_subcarrier_spacing_t scs           = srsran_subcarrier_spacing_30kHz;

void parse_args(int argc, char* argv[])
{
  int opt;
  while ((opt = getopt(argc, argv, "sfedbSrg")) != -1) {
    switch (opt) {
      case 's': {
        double srateMHz = atof(argv[optind]);
        sample_rate     = srateMHz * 1e6;
        printf("Sample rate: %f MHz\n", srateMHz);
        break;
      }
      case 'f': {
        double startFreqMHz = atof(argv[optind]);
        start_freq          = startFreqMHz * 1e6;
        sf_len              = sample_rate * SF_DURATION;
        printf("Start frequency: %f MHz\n", startFreqMHz);
        break;
      }
      case 'e': {
        double stopFreqMHz = atof(argv[optind]);
        stop_freq          = stopFreqMHz * 1e6;
        printf("Stop frequency: %f MHz\n", stopFreqMHz);
        break;
      }
      case 'd': {
        source_params = argv[optind];
        printf("Source params: %s\n", source_params.c_str());
        break;
      }
      case 'b': {
        band = atoi(argv[optind]);
        break;
      }
      case 'S': {
        scs = srsran_subcarrier_spacing_from_str(argv[optind]);
        break;
      }
      case 'r': {
        rounds = atoi(argv[optind]);
        break;
      }
      case 'g': {
        rx_gain = atoi(argv[optind]);
        break;
      }
      default:
        fprintf(stderr, "Unknown option: %c\n", opt);
        exit(EXIT_FAILURE);
    }
  }
}

void scan_ssb(Source*                     source,
              double                      srate,
              double                      ssb_freq,
              srslog::basic_logger&       logger,
              srsran_subcarrier_spacing_t scs,
              uint32_t                    round = 100)
{
  srsran_ssb_t       ssb = {};
  srsran_timestamp_t ts  = {};

  if (!init_ssb(ssb, srate, ssb_freq, ssb_freq, scs, SRSRAN_SSB_PATTERN_C, SRSRAN_DUPLEX_MODE_TDD)) {
    logger.error("Error initializing SSB");
    srsran_ssb_free(&ssb);
    return;
  }

  source->set_rx_freq(ssb_freq);
  cf_t* buffer = srsran_vec_cf_malloc(sf_len);
  cf_t* rx_buffer[SRSRAN_MAX_CHANNELS];
  for (uint32_t i = 0; i < SRSRAN_MAX_CHANNELS; i++) {
    rx_buffer[i] = nullptr;
  }
  rx_buffer[0] = buffer;
  for (uint32_t i = 0; i < round; i++) {
    /* Receive samples */
    source->recv(rx_buffer, sf_len * 0.1, &ts);
    source->recv(rx_buffer, sf_len, &ts);
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
    srsran_csi_trs_measurements_t& measure = res.measurements;
    logger.info("Found cell: id: %u %s SNR: %f dB CFO: %f Hz RSRP: %f",
                res.N_id,
                mib_info_str.data(),
                measure.snr_dB,
                measure.cfo_hz,
                measure.rsrp_dB);

    char filename[64];
    sprintf(filename, "ssb_%u_%f", res.t_offset, ssb_freq);
    write_record_to_file(buffer, sf_len, filename);
    break;
  }
cleanup:
  srsran_ssb_free(&ssb);
  free(buffer);
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  /* Initialize logger */
  config.log_level             = srslog::basic_levels::info;
  srslog::basic_logger& logger = srslog_init(&config);

  /* Initialize source */
  ShadowerConfig config = {};
  config.source_type    = "uhd";
  config.source_module  = uhd_source_module_path;
  config.source_params  = source_params;
  config.sample_rate    = sample_rate;
  config.dl_freq        = start_freq;
  config.ul_freq        = start_freq;
  config.rx_gain        = rx_gain;
  config.tx_gain        = 0;

  create_source_t uhd_source_creator = load_source(uhd_source_module_path);
  Source*         source             = uhd_source_creator(config);

  srsran::srsran_band_helper                band_helper;
  srsran::srsran_band_helper::sync_raster_t sync_raster = band_helper.get_sync_raster(band, scs);
  if (!sync_raster.valid()) {
    logger.error("Invalid band %d or SCS %d kHz\n", band, scs);
    exit(1);
  }

  while (!sync_raster.end()) {
    double ssb_freq = sync_raster.get_frequency();
    if (ssb_freq < start_freq) {
      sync_raster.next();
      continue;
    }
    if (ssb_freq > stop_freq) {
      break;
    }
    logger.info("Scanning SSB at %f MHz", ssb_freq / 1e6);
    scan_ssb(source, sample_rate, ssb_freq, logger, scs, rounds);
    sync_raster.next();
  }
  source->close();
}
