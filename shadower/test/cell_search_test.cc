#include "shadower/hdr/utils.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srslog/srslog.h"
#include "test_variables.h"
#include <fstream>

#if TEST_TYPE == 1
std::string sample_file = "shadower/test/data/srsran/ssb.fc32";
#elif TEST_TYPE == 2
std::string sample_file = "shadower/test/data/ssb.fc32";
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/sib.fc32";
#endif // TEST_TYPE
int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* initialize ssb */
  srsran_ssb_t ssb = {};
  if (!init_ssb(ssb, srate, dl_freq, ssb_freq, scs, pattern, duplex)) {
    logger.error("Failed to initialize SSB");
    return -1;
  }

  /* load samples from file */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  /* search for SSB */
  srsran_ssb_search_res_t res = {};
  if (srsran_ssb_search(&ssb, samples.data(), sf_len, &res) < SRSRAN_SUCCESS) {
    logger.error("Error running srsran_ssb_search");
    return -1;
  }
  if (res.measurements.snr_dB < -10.0f || !res.pbch_msg.crc) {
    logger.error("Failed to decode PBCH message");
    return -1;
  }

  /* decode MIB */
  srsran_mib_nr_t mib = {};
  if (srsran_pbch_msg_nr_mib_unpack(&res.pbch_msg, &mib) < SRSRAN_SUCCESS) {
    logger.error("Error running srsran_pbch_msg_nr_mib_unpack");
    return -1;
  }

  /* get SSB index */
  uint32_t sf_idx   = srsran_ssb_candidate_sf_idx(&ssb, res.pbch_msg.ssb_idx, res.pbch_msg.hrf);
  uint32_t slot_idx = mib.sfn * 10 * slots_per_sf + sf_idx;
  logger.info("SF index: %u Slot index: %u", sf_idx, slot_idx);

  /* write MIB to file */
  std::array<char, 512> mib_info_str = {};
  srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(), mib_info_str.size());
  std::ofstream mib_raw{mib_config_raw, std::ios::binary};
  mib_raw.write(reinterpret_cast<const char*>(&mib), sizeof(mib));

  logger.info("Found cell: %s", mib_info_str.data());
  logger.info("Cell id: %u", res.N_id);
  logger.info("Offset: %u", res.t_offset);
  logger.info("CFO: %f", res.measurements.cfo_hz);
  return 0;
}