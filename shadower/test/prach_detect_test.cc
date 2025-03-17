#include "shadower/hdr/utils.h"
#include "srsran/mac/mac_rar_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/phch/prach.h"
#include "test_variables.h"
#include <fstream>

#if TEST_TYPE == 1
std::string sample_file = "shadower/test/data/srsran/prach.fc32";
#elif TEST_TYPE == 2
std::string sample_file = "shadower/test/data/prach.fc32";
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/prach.fc32";
#endif // TEST_TYPE

int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* initialize phy cfg */
  srsran::phy_cfg_nr_t phy_cfg = {};
  init_phy_cfg(phy_cfg, config);

  /* load mib configuration and update phy_cfg */
  if (!configure_phy_cfg_from_mib(phy_cfg, mib_config_raw, ncellid)) {
    printf("Failed to configure phy cfg from mib\n");
    return -1;
  }

  /* load sib1 configuration and apply to phy_cfg */
  if (!configure_phy_cfg_from_sib1(phy_cfg, sib1_config_raw, sib1_size)) {
    logger.error("Failed to configure phy cfg from sib1");
    return -1;
  }

  /* Initialize prach */
  srsran_prach_t prach = {};
  if (srsran_prach_init(&prach, srsran_symbol_sz(config.nof_prb))) {
    logger.error("Failed to initialized PRACH");
    return -1;
  }

  /* Update prach with configuration from phy_cfg */
  if (srsran_prach_set_cfg(&prach, &phy_cfg.prach, config.nof_prb)) {
    logger.error("Failed to set PRACH config");
    return -1;
  }
  srsran_prach_set_detect_factor(&prach, 60);

  /* load test samples */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  /* Run prach detection */
  uint32_t prach_indices[165] = {};
  float    prach_offsets[165] = {};
  float    prach_p2avg[165]   = {};
  uint32_t prach_nof_det      = 0;
  if (srsran_prach_detect_offset(&prach,
                                 phy_cfg.pdcch.coreset->offset_rb,
                                 samples.data(),
                                 sf_len - prach.N_cp,
                                 prach_indices,
                                 prach_offsets,
                                 prach_p2avg,
                                 &prach_nof_det)) {
    logger.error("Failed to detect PRACH");
    return -1;
  }

  logger.info("Detected %u PRACH signals", prach_nof_det);

  if (prach_nof_det) {
    logger.info("Detected %u PRACH signals", prach_nof_det);
    for (uint32_t i = 0; i < prach_nof_det; i++) {
      logger.info("PRACH:  preamble=%d, offset=%.1f us, peak2avg=%.1f",
                  prach_indices[i],
                  prach_offsets[i] * 1e6,
                  prach_p2avg[i]);
    }
  }
}