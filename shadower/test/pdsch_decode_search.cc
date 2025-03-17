#include "shadower/hdr/utils.h"
#include "shadower/hdr/wd_worker.h"
#include "shadower/test/dummy_exploit.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "test_variables.h"
#include <iomanip>
#include <sstream>

uint16_t           rnti      = c_rnti;
srsran_rnti_type_t rnti_type = srsran_rnti_type_c;

#if TEST_TYPE == 1
std::string sample_file = "shadower/test/data/srsran/pdsch_6400.fc32";
std::string last_file   = "shadower/test/data/srsran/pdsch_6400.fc32";
uint8_t     half        = 0;
float       cfo         = 0;
#elif TEST_TYPE == 2
std::string sample_file = "/root/overshadow/effnet/sf_152_11864.fc32";
uint8_t     half        = 1;
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/pdsch_12722.fc32";
std::string last_file   = "shadower/test/data/srsran-n78-40MHz/pdsch_12722.fc32";
uint8_t     half        = 1;
#endif // TEST_TYPE

int main(int argc, char* argv[])
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::debug);

  /* initialize phy cfg */
  srsran::phy_cfg_nr_t phy_cfg = {};
  init_phy_cfg(phy_cfg, config);

  /* init phy state */
  srsue::nr::state phy_state = {};
  init_phy_state(phy_state, config.nof_prb);

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

  /* load rrc_setup cell configuration and apply to phy_cfg */
  if (!configure_phy_cfg_from_rrc_setup(phy_cfg, rrc_setup_raw, rrc_setup_size, logger)) {
    logger.error("Failed to configure phy cfg from rrc setup");
    return -1;
  }

  /* UE DL init with configuration from phy_cfg */
  srsran_ue_dl_nr_t ue_dl        = {};
  cf_t*             ue_dl_buffer = srsran_vec_cf_malloc(sf_len);
  if (!init_ue_dl(ue_dl, ue_dl_buffer, phy_cfg)) {
    logger.error("Failed to init UE DL");
    return -1;
  }

  /* Parse command line arguments as test arguments */
  test_args_t args = parse_test_args(argc, argv);
  if (args.rnti != 0) {
    rnti = args.rnti;
  }

  /* load test samples */
  std::vector<cf_t> samples(sf_len);
  if (!args.sample_filename.empty()) {
    sample_file = args.sample_filename;
  }
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  std::vector<cf_t> last_samples(sf_len);
  if (!load_samples(last_file, last_samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", last_file.c_str());
    return -1;
  }

  /* Retrieve the slot index from file name */
  uint32_t slot_number = parse_slot_idx_from_filename(sample_file);
  if (!args.sample_filename.empty()) {
    half        = args.half;
    slot_number = args.slot_idx;
  }

  /* Pre-initialize softbuffer rx */
  srsran_softbuffer_rx_t softbuffer_rx = {};
  if (srsran_softbuffer_rx_init_guru(&softbuffer_rx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) !=
      0) {
    logger.error("Couldn't allocate and/or initialize softbuffer");
    return -1;
  }
  /* Run wdissector for packet summary */
  WDWorker*                        wd_worker = new WDWorker(config.duplex_mode, config.bc_worker_log_level);
  SafeQueue<std::vector<uint8_t> > dl_msg_queue;
  SafeQueue<std::vector<uint8_t> > ul_msg_queue;
  DummyExploit*                    exploit = new DummyExploit(dl_msg_queue, ul_msg_queue);

  /* Assume the slot number is wrong, brute force search 20 possible slot numbers to search */
  for (uint32_t i = 0; i < 20; i++) {
    /* For each slot number try different delays from -100 to 500 */
    for (int32_t delay = -100; delay < 500; delay += 1) {
      /* Initialize slot cfg */
      srsran_slot_cfg_t slot_cfg = {.idx = slot_number + half + i};

      if (half == 0 && delay < 0) {
        /* copy samples to ue_dl processing buffer */
        srsran_vec_cf_copy(ue_dl_buffer, last_samples.data() + sf_len + delay, -delay);
        srsran_vec_cf_copy(ue_dl_buffer - delay, samples.data(), slot_len + delay);
      } else {
        /* copy samples to ue_dl processing buffer */
        srsran_vec_cf_copy(ue_dl_buffer, samples.data() + half * slot_len + delay, slot_len);
      }
#if TEST_TYPE == 1
      srsran_vec_apply_cfo(ue_dl_buffer, -cfo / srate, ue_dl_buffer, slot_len);
#endif // TEST_TYPE
      /* run ue_dl estimate fft */
      srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);
      /* search for dci */
      ue_dl_dci_search(ue_dl, phy_cfg, slot_cfg, rnti, rnti_type, phy_state, logger);

      /* get grant from dci search */
      uint32_t                   pid          = 0;
      srsran_sch_cfg_nr_t        pdsch_cfg    = {};
      srsran_harq_ack_resource_t ack_resource = {};
      if (!phy_state.get_dl_pending_grant(slot_cfg.idx, pdsch_cfg, ack_resource, pid)) {
        continue;
      }
      /* Initialize the buffer for output*/
      srsran::unique_byte_buffer_t data = srsran::make_byte_buffer();
      if (data == nullptr) {
        logger.error("Error creating byte buffer");
        return -1;
      }
      data->N_bytes = pdsch_cfg.grant.tb[0].tbs / 8U;

      /* Initialize pdsch result*/
      srsran_pdsch_res_nr_t pdsch_res = {};
      pdsch_res.tb[0].payload         = data->msg;
      srsran_softbuffer_rx_reset(&softbuffer_rx);

      /* Decode PDSCH */
      if (!ue_dl_pdsch_decode(ue_dl, pdsch_cfg, slot_cfg, pdsch_res, softbuffer_rx, logger)) {
        logger.error("Failed to decode PDSCH");
        continue;
      }

      /* if the message is not decoded correctly, then try another settings */
      if (!pdsch_res.tb[0].crc) {
        logger.debug("Error PDSCH got wrong CRC %d", delay);
        continue;
      }
      logger.info("Successfully decoded at delay %d slot %u", delay, slot_cfg.idx);

      /* Print received messages bytes in hex */
      std::ostringstream oss;
      for (uint32_t i = 0; i < data->N_bytes; i++) {
        oss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(data->msg[i]) << ", ";
      }
      logger.info("Decoded message: %s", oss.str().c_str());

      wd_worker->process(data->msg, data->N_bytes, rnti, 0, 0, slot_cfg.idx, DL, exploit);
    }
  }
  return 0;
}
