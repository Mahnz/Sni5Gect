#include "shadower/source/source.h"
#include "shadower/utils/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <future>
#include <mutex>

std::string source_type  = "file";
std::string source_param = "shadower/test/data/srsran-n78-20MHz/sib.fc32";
double      center_freq  = 3427.5e6;  // Center frequency for SSB search
double      ssb_freq     = 3421.92e6; // SSB frequency to search
double      range        = 20e3;      // Frequency gap for SSB search
double      srate        = 23.04e6;   // Sample rate (Hz)
uint32_t    band         = 78;        // Band number for SSB search

srsran_subcarrier_spacing_t scs     = srsran_subcarrier_spacing_30kHz;
srsran_ssb_pattern_t        pattern = SRSRAN_SSB_PATTERN_C;
srsran_duplex_mode_t        duplex  = SRSRAN_DUPLEX_MODE_TDD;

void parse_args(int argc, char* argv[])
{
  int opt;
  while ((opt = getopt(argc, argv, "fFrdpsbc")) != -1) {
    switch (opt) {
      case 'f': {
        double centerFreqMHz = atof(argv[optind]);
        center_freq          = centerFreqMHz * 1e6;
        printf("Using center frequency %f\n", center_freq / 1e6);
        break;
      }
      case 'F': {
        double ssbFreqMHz = atof(argv[optind]);
        ssb_freq          = ssbFreqMHz * 1e6;
        printf("Using SSB frequency %f\n", ssb_freq / 1e6);
        break;
      }
      case 'r': {
        double rangeKHz = atof(argv[optind]);
        range           = rangeKHz * 1e3;
        printf("Using frequency range %f\n", range / 1e3);
        break;
      }
      case 'd': {
        source_type = argv[optind];
        printf("Using source type %s\n", source_type.c_str());
        break;
      }
      case 'p': {
        source_param = argv[optind];
        printf("Using source parameter %s\n", source_param.c_str());
        break;
      }
      case 's': {
        double sampleRateMHz = atof(argv[optind]);
        srate                = sampleRateMHz * 1e6;
        printf("Using sample rate %f\n", srate / 1e6);
        break;
      }
      case 'b': {
        band = atoi(argv[optind]);
        printf("Using band %d\n", band);
        break;
      }
      case 'c': {
        int scsKhz = atoi(argv[optind]);
        printf("Using subcarrier spacing %d\n", scsKhz);
        scs = srsran_subcarrier_spacing_from_str(std::to_string(scsKhz).c_str());
        break;
      }
      default:
        fprintf(stderr, "Unknown option: %c\n", opt);
        exit(EXIT_FAILURE);
    }
  }

  srsran::srsran_band_helper helper;
  pattern = srsran::srsran_band_helper::get_ssb_pattern(band, scs);
  duplex  = helper.get_duplex_mode(band);
}

int main(int argc, char* argv[])
{
  parse_args(argc, argv);
  /* initialize logger */
  ShadowerConfig config        = {};
  config.log_level             = srslog::basic_levels::debug;
  srslog::basic_logger& logger = srslog_init(&config);
  logger.set_level(srslog::basic_levels::debug);

  /* Load IQ samples from file */
  uint32_t sf_len      = srate * SF_DURATION;
  config.source_type   = source_type;
  config.source_params = source_param;
  config.source_srate  = srate;
  Source* source;
  if (source_type == "file") {
    create_source_t file_source_creator = load_source(file_source_module_path);
    source                              = file_source_creator(config);
  } else if (source_type == "uhd") {
    config.source_module               = uhd_source_module_path;
    config.source_srate                = srate;
    config.sample_rate                 = srate;
    create_source_t uhd_source_creator = load_source(uhd_source_module_path);
    source                             = uhd_source_creator(config);
  } else {
    logger.error("Unsupported source type: %s", source_type.c_str());
    return -1;
  }

  std::vector<cf_t> samples(sf_len);
  cf_t*             channels[SRSRAN_MAX_CHANNELS];
  channels[0] = samples.data();
  while (true) {
    srsran_timestamp_t ts  = {};
    int                ret = source->recv(channels, sf_len, &ts);
    if (ret < 0) {
      logger.error("Failed to receive samples from source");
      break;
    }

    for (double freq = ssb_freq - range; freq < ssb_freq + range; freq += 1e3) {
      /* initialize ssb */
      srsran_ssb_t ssb = {};
      if (!init_ssb(ssb, srate, center_freq, freq, scs, pattern, duplex)) {
        logger.error("Failed to initialize SSB");
      }

      /* Search for SSB */
      srsran_ssb_search_res_t res = {};
      if (srsran_ssb_search(&ssb, samples.data(), sf_len, &res) < SRSRAN_SUCCESS) {
        logger.error("Error running srsran_ssb_search");
        return -1;
      }
      if (!res.pbch_msg.crc) {
        logger.error("Failed to decode PBCH message %f", freq / 1e6);
        continue;
      }
      printf("Center freq: %f SSB freq: %f CFO: %f\n", center_freq / 1e6, freq / 1e6, res.measurements.cfo_hz);
      srsran_ssb_free(&ssb);
    }
  }
  return 0;
}