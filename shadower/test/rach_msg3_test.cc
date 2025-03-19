#include "shadower/hdr/utils.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "test_variables.h"

uint16_t           rnti      = c_rnti;
srsran_rnti_type_t rnti_type = srsran_rnti_type_c;
#if TEST_TYPE == 1
uint32_t    rach_msg2_slot_idx = 3330;
uint32_t    rach_msg3_slot_idx = 3336;
std::string sample_file        = "shadower/test/data/srsran-n78-20MHz/rach_msg3.fc32";
uint8_t     half               = 1;
#elif TEST_TYPE == 2
uint32_t    rach_msg2_slot_idx = 11645;
uint32_t    rach_msg3_slot_idx = 11648;
std::string sample_file        = "shadower/test/data/rach_msg3.fc32";
uint8_t     half               = 1;
#elif TEST_TYPE == 3
uint32_t    rach_msg2_slot_idx = 13550;
uint32_t    rach_msg3_slot_idx = 13556;
std::string sample_file        = "shadower/test/data/srsran-n78-40MHz/rach_msg3.fc32";
uint8_t     half               = 1;
#endif // TEST_TYPE
int main()
{
  /* initialize logger */
  srslog::basic_logger& logger = srslog_init();
  logger.set_level(srslog::basic_levels::info);

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

  /* GNB UL initialize with configuration from phy_cfg */
  srsran_gnb_ul_t gnb_ul        = {};
  cf_t*           gnb_ul_buffer = srsran_vec_cf_malloc(sf_len);
  if (!init_gnb_ul(gnb_ul, gnb_ul_buffer, phy_cfg)) {
    logger.error("Failed to init GNB UL");
    return -1;
  }

  /* load raw grant data carried in rach msg2 */
  std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS> ul_grant_raw{};
  if (!read_raw_config(rach_msg2_ul_grant_file, ul_grant_raw.data(), SRSRAN_RAR_UL_GRANT_NBITS)) {
    logger.error("Failed to read RAR UL grant from %s", rach_msg2_ul_grant_file.c_str());
    return -1;
  }

  /* load test samples */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  srsran_softbuffer_rx_t softbuffer_rx = {};
  if (srsran_softbuffer_rx_init_guru(&softbuffer_rx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) !=
      0) {
    logger.error("Couldn't allocate and/or initialize softbuffer");
    return -1;
  }

  bool found = false;
  for (double cfo = uplink_cfo - 0.02; cfo < uplink_cfo + 0.02; cfo += 0.00001) {
    /* Add rar grant to phy_state */
    if (!set_rar_grant(rnti, rnti_type, rach_msg2_slot_idx, ul_grant_raw, phy_cfg, phy_state, logger)) {
      logger.error("Failed to set RAR grant");
      return -1;
    }

    /* copy samples to gnb_ul processing buffer */
    srsran_vec_cf_copy(gnb_ul_buffer, samples.data() + half * slot_len - ul_sample_offset, slot_len);
    srsran_slot_cfg_t slot_cfg = {.idx = rach_msg3_slot_idx + half};

    /* get uplink grant */
    uint32_t            pid       = 0;
    srsran_sch_cfg_nr_t pusch_cfg = {};
    if (!phy_state.get_ul_pending_grant(slot_cfg.idx, pusch_cfg, pid)) {
      logger.error("No uplink grant available");
      return -1;
    }

    /* run gnb_ul estimate fft */
    if (srsran_gnb_ul_fft(&gnb_ul)) {
      logger.error("Error running srsran_gnb_ul_fft");
      return -1;
    }

    char filename[64];
    sprintf(filename, "ofdm_rach_msg3_fft%u", nof_sc);
    write_record_to_file(gnb_ul.sf_symbols[0], nof_re, filename);

    /* Apply the cfo to the signal with magic number */
    srsran_vec_apply_cfo(gnb_ul.sf_symbols[0], cfo, gnb_ul.sf_symbols[0], nof_re);

    /* Initialize the buffer for output*/
    srsran::unique_byte_buffer_t data = srsran::make_byte_buffer();
    if (data == nullptr) {
      logger.error("Error creating byte buffer");
      return -1;
    }
    data->N_bytes = pusch_cfg.grant.tb[0].tbs / 8U;

    /* Initialize pusch result*/
    srsran_pusch_res_nr_t pusch_res = {};
    pusch_res.tb[0].payload         = data->msg;
    srsran_softbuffer_rx_reset(&softbuffer_rx);

    /* Decode PUSCH */
    if (!gnb_ul_pusch_decode(gnb_ul, pusch_cfg, slot_cfg, pusch_res, softbuffer_rx, logger)) {
      logger.error("Error running gnb_ul_pusch_decode");
      continue;
    }

    /* if the message is not decoded correctly, then return */
    if (!pusch_res.tb[0].crc) {
      continue;
    } else {
      logger.info("PUSCH CRC passed Delay: %u CFO: %f", ul_sample_offset, cfo);
    }
  }
  return 0;
}