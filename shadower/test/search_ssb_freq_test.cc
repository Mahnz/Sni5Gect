#include "shadower/hdr/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include "test_variables.h"
#include <fstream>

#if TEST_TYPE == 1
std::string sample_file = "shadower/test/data/srsran-n78-20MHz/sib.fc32";
#elif TEST_TYPE == 2
std::string sample_file = "shadower/test/data/sib1.fc32";
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/sib.fc32";
#endif // TEST_TYPE

int main(int argc, char* argv[])
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* load samples from file */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  for (double ssb_freq = config.ssb_freq - 30e3; ssb_freq < config.ssb_freq + 30e3; ssb_freq += 1e3) {
    /* initialize ssb */
    std::shared_ptr<srsran_ssb_t> ssb = std::make_shared<srsran_ssb_t>();
    if (!init_ssb(*ssb,
                  config.sample_rate,
                  config.dl_freq,
                  ssb_freq,
                  config.scs_ssb,
                  config.ssb_pattern,
                  config.duplex_mode)) {
      logger.error("Failed to initialize SSB");
      return -1;
    }

    /* search for SSB */
    srsran_ssb_search_res_t res = {};
    if (srsran_ssb_search(ssb.get(), samples.data(), sf_len, &res) < SRSRAN_SUCCESS) {
      logger.error("Error running srsran_ssb_search");
      return -1;
    }

    if (res.N_id != ncellid) {
      continue;
    }
    logger.info("\n############################\nSSB Freq: %f MHz", ssb_freq / 1e6);
    logger.info("Cell id: %u", res.N_id);
    logger.info("Offset: %u", res.t_offset);
    logger.info("CFO: %f", res.measurements.cfo_hz);
  }
  return 0;
}