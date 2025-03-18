#include "shadower/hdr/broadcast_worker.h"
#include "shadower/hdr/utils.h"
#include "test_variables.h"

#if TEST_TYPE == 1
std::string sib_sample_file    = "shadower/test/data/srsran-n78-20MHz/sib.fc32";
uint32_t    sib_slot_idx       = 0;
std::string rach_msg2_file     = "shadower/test/data/srsran-n78-20MHz/rach_msg2.fc32";
uint32_t    rach_msg2_slot_idx = 3330;
#elif TEST_TYPE == 2
std::string sib_sample_file    = "shadower/test/data/sib1.fc32";
uint32_t    sib_slot_idx       = 5484;
std::string rach_msg2_file     = "shadower/test/data/rach_msg2.fc32";
uint32_t    rach_msg2_slot_idx = 5564;
#elif TEST_TYPE == 3
std::string sib_sample_file    = "shadower/test/data/srsran-n78-40MHz/sib.fc32";
uint32_t    sib_slot_idx       = 0;
std::string rach_msg2_file     = "shadower/test/data/srsran-n78-40MHz/rach_msg2.fc32";
uint32_t    rach_msg2_slot_idx = 12570;
#endif // TEST_TYPE

int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);
  config.bc_worker_log_level = srslog::basic_levels::debug;

  /* initialize phy cfg */
  srsran::phy_cfg_nr_t phy_cfg = {};
  init_phy_cfg(phy_cfg, config);

  /* load mib configuration and update phy_cfg */
  if (!configure_phy_cfg_from_mib(phy_cfg, mib_config_raw, ncellid)) {
    printf("Failed to configure phy cfg from mib\n");
    return -1;
  }

  /* Update broadcast worker with mib */
  BroadCastWorker broadcast_worker(config);
  /* Read mib from file */
  srsran_mib_nr_t mib = {};
  if (!read_raw_config(mib_config_raw, (uint8_t*)&mib, sizeof(srsran_mib_nr_t))) {
    return -1;
  }
  broadcast_worker.apply_config_from_mib(mib, ncellid);
  broadcast_worker.on_sib1_found = [&](asn1::rrc_nr::sib1_s& sib1) { broadcast_worker.apply_config_from_sib1(sib1); };

  /* work on the SIB1 samples */
  std::shared_ptr<Task>               task    = std::make_shared<Task>();
  std::shared_ptr<std::vector<cf_t> > samples = std::make_shared<std::vector<cf_t> >(sf_len);
  if (!load_samples(sib_sample_file, samples->data(), sf_len)) {
    printf("Failed to load samples\n");
    return -1;
  }
  task->buffer   = samples;
  task->slot_idx = sib_slot_idx;
  task->ts       = {};
  broadcast_worker.work(task);

  /* work on the RACH msg2 samples */
  std::shared_ptr<Task>               task2     = std::make_shared<Task>();
  std::shared_ptr<std::vector<cf_t> > rach_msg2 = std::make_shared<std::vector<cf_t> >(sf_len);
  if (!load_samples(rach_msg2_file, rach_msg2->data(), sf_len)) {
    printf("Failed to load samples\n");
    return -1;
  }
  task2->buffer   = rach_msg2;
  task2->slot_idx = rach_msg2_slot_idx;
  task2->ts       = {};
  broadcast_worker.work(task2);
  return 0;
}