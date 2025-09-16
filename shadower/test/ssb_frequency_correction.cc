#include "shadower/hdr/syncer.h"
#include "shadower/hdr/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include "test_variables.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <future>
#include <mutex>

std::string sample_file = "shadower/test/data/test-n78-100MHz/ssb.fc32";

int main(int argc, char* argv[])
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);
  double                      ssb_target_freq = 3525.6e6;
  double                      dl_freq         = 3500.01e6;
  double                      gap             = 30e3;
  double                      srate           = 122.88e6;
  srsran_subcarrier_spacing_t scs             = srsran_subcarrier_spacing_30kHz;
  srsran_ssb_pattern_t        pattern         = SRSRAN_SSB_PATTERN_C;
  srsran_duplex_mode_t        duplex          = SRSRAN_DUPLEX_MODE_TDD;
  logger.info("SSB target frequency: %f", ssb_target_freq / 1e6);

  /* Load IQ samples from file */
  uint32_t          sf_len = srate * SF_DURATION;
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }
  double center_freq = dl_freq;
  for (double ssb_freq = ssb_target_freq - gap; ssb_freq < ssb_target_freq + gap; ssb_freq += 1e3) {
    /* initialize ssb */
    srsran_ssb_t ssb = {};
    if (!init_ssb(ssb, srate, center_freq, ssb_freq, scs, pattern, duplex)) {
      logger.error("Failed to initialize SSB");
    }

    /* Search for SSB */
    srsran_ssb_search_res_t res = {};
    if (srsran_ssb_search(&ssb, samples.data(), sf_len, &res) < SRSRAN_SUCCESS) {
      logger.error("Error running srsran_ssb_search");
      return -1;
    }
    if (!res.pbch_msg.crc) {
      logger.error("Failed to decode PBCH message %f", ssb_freq / 1e6);
      continue;
    }
    printf("Center freq: %f SSB freq: %f CFO: %f\n", center_freq / 1e6, ssb_freq / 1e6, res.measurements.cfo_hz);
    srsran_ssb_free(&ssb);
  }
  return 0;
}