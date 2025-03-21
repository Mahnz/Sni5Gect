#include "shadower/hdr/utils.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "srsran/srslog/srslog.h"
#include "test_variables.h"
#include <fstream>
#if ENABLE_CUDA
#include "shadower/hdr/fft_processor.cuh"
#endif // ENABLE_CUDA

uint16_t           rnti      = si_rnti;
srsran_rnti_type_t rnti_type = srsran_rnti_type_si;

#if TEST_TYPE == 1
std::string sample_file = "shadower/test/data/srsran-n78-20MHz/sib.fc32";
uint32_t    slot_number = 1;
#elif TEST_TYPE == 2
std::string sample_file = "shadower/test/data/sib1.fc32";
uint32_t    slot_number = 11604;
float       cfo         = 0;
#elif TEST_TYPE == 3
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/sib.fc32";
uint32_t    slot_number = 1;
#endif // TEST_TYPE
int main()
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
    logger.error("Failed to configure phy cfg from mib");
    return -1;
  }

  /* UE DL init with configuration from phy_cfg */
  srsran_ue_dl_nr_t ue_dl  = {};
  cf_t*             buffer = srsran_vec_cf_malloc(sf_len);
  if (!init_ue_dl(ue_dl, buffer, phy_cfg)) {
    logger.error("Failed to init UE DL");
    return -1;
  }

  /* load test samples */
  std::vector<cf_t> samples(sf_len);
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }
#if TEST_TYPE == 1 || TEST_TYPE == 3
  /* copy samples to ue_dl processing buffer */
  srsran_vec_cf_copy(buffer, samples.data() + slot_len, slot_len);
#elif TEST_TYPE == 2
  /* copy samples to ue_dl processing buffer */
  srsran_vec_cf_copy(buffer, samples.data(), slot_len);
#endif // TEST_TYPE
  /* Initialize slot cfg */
  srsran_slot_cfg_t slot_cfg = {.idx = slot_number};
  /* run ue_dl estimate fft */
  srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);

#if ENABLE_CUDA
  FFTProcessor fft_processor(
      config.sample_rate, ue_dl.carrier.dl_center_frequency_hz, ue_dl.carrier.scs, &ue_dl.fft[0]);
  fft_processor.to_ofdm(buffer, ue_dl.sf_symbols[0], slot_cfg.idx);
#endif // ENABLE_CUDA

  /* search for dci */
  ue_dl_dci_search(ue_dl, phy_cfg, slot_cfg, rnti, rnti_type, phy_state, logger);

  /* get grant from dci search */
  uint32_t                   pid          = 0;
  srsran_sch_cfg_nr_t        pdsch_cfg    = {};
  srsran_harq_ack_resource_t ack_resource = {};
  if (!phy_state.get_dl_pending_grant(slot_cfg.idx, pdsch_cfg, ack_resource, pid)) {
    logger.error("Failed to get grant from dci search");
    return -1;
  }

  /* Initialize the buffer for output*/
  srsran::unique_byte_buffer_t data = srsran::make_byte_buffer();
  if (data == nullptr) {
    logger.error("Error creating byte buffer");
    return -1;
  }
  data->N_bytes = pdsch_cfg.grant.tb[0].tbs / 8U;

  /* Initialize pdsch result*/
  srsran_pdsch_res_nr_t pdsch_res      = {};
  pdsch_res.tb[0].payload              = data->msg;
  srsran_softbuffer_rx_t softbuffer_rx = {};
  if (srsran_softbuffer_rx_init_guru(&softbuffer_rx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) !=
      0) {
    logger.error("Couldn't allocate and/or initialize softbuffer");
    return -1;
  }

  /* Decode PDSCH */
  if (!ue_dl_pdsch_decode(ue_dl, pdsch_cfg, slot_cfg, pdsch_res, softbuffer_rx, logger)) {
    return -1;
  }

  /* if the message is not decoded correctly, then return */
  if (!pdsch_res.tb[0].crc) {
    logger.debug("Error PDSCH got wrong CRC");
    return -1;
  }

  /* decode SIB1 */
  asn1::rrc_nr::sib1_s sib1;
  if (!parse_to_sib1(data->msg, data->N_bytes, sib1)) {
    logger.error("Error decoding SIB1");
    return -1;
  }
  asn1::json_writer json_writer;
  sib1.to_json(json_writer);
  logger.info("SIB1: %s", json_writer.to_string().c_str());

  /* Write the SIB json to file */
  std::ofstream sib1_json{"logs/sib1.json"};
  sib1_json << json_writer.to_string() << std::endl;

  /* write SIB1 to file */
  std::ofstream received_sib1{sib1_config_raw, std::ios::binary};
  received_sib1.write(reinterpret_cast<char*>(data->msg), data->N_bytes);
  logger.info("SIB1 number of bytes: %u", data->N_bytes);
  return 0;
}