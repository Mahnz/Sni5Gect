#include "shadower/hdr/utils.h"
#include "shadower/hdr/wd_worker.h"
#include "shadower/test/dummy_exploit.h"
#include "srsran/mac/mac_rar_pdu_nr.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "test_variables.h"
#include <fstream>
#if ENABLE_CUDA
#include "shadower/hdr/fft_processor.cuh"
#endif // ENABLE_CUDA

uint16_t           rnti      = c_rnti;
srsran_rnti_type_t rnti_type = srsran_rnti_type_c;

#if TEST_TYPE == 1
// std::string dci_sample_file  = "shadower/test/data/srsran-n78-20MHz/dci_ul_3422.fc32";
// std::string sample_file      = "shadower/test/data/srsran-n78-20MHz/pusch_3426.fc32";
// std::string last_sample_file = sample_file;
// uint8_t     half             = 1;
// std::string dci_sample_file  = "shadower/test/data/srsran-n78-20MHz/dci_ul_3462.fc32";
// std::string sample_file      = "shadower/test/data/srsran-n78-20MHz/pusch_3466.fc32";
// std::string last_sample_file = sample_file;
// uint8_t     half             = 1;
std::string dci_sample_file  = "shadower/test/data/srsran-n78-20MHz/dci_ul_3622.fc32";
std::string sample_file      = "shadower/test/data/srsran-n78-20MHz/pusch_3626.fc32";
std::string last_sample_file = sample_file;
uint8_t     half             = 1;
#elif TEST_TYPE == 2
std::string dci_sample_file  = "shadower/test/data/dci_11686.fc32";
std::string sample_file      = "shadower/test/data/dci_11688.fc32";
std::string last_sample_file = dci_sample_file;
uint8_t     half             = 1;
#elif TEST_TYPE == 3
// std::string dci_sample_file  = "shadower/test/data/srsran-n78-40MHz/dci_ul_13622.fc32";
// std::string sample_file      = "shadower/test/data/srsran-n78-40MHz/pusch_13626.fc32";
// std::string last_sample_file = sample_file;
// uint8_t     half             = 1;
// std::string dci_sample_file  = "shadower/test/data/srsran-n78-40MHz/dci_ul_13662.fc32";
// std::string sample_file      = "shadower/test/data/srsran-n78-40MHz/pusch_13666.fc32";
// std::string last_sample_file = sample_file;
// uint8_t     half             = 1;
// std::string dci_sample_file  = "shadower/test/data/srsran-n78-40MHz/dci_ul_13702.fc32";
// std::string sample_file      = "shadower/test/data/srsran-n78-40MHz/pusch_13706.fc32";
// std::string last_sample_file = sample_file;
// uint8_t     half             = 1;
std::string dci_sample_file  = "shadower/test/data/srsran-n78-40MHz/dci_ul_13782.fc32";
std::string sample_file      = "shadower/test/data/srsran-n78-40MHz/pusch_13786.fc32";
std::string last_sample_file = sample_file;
uint8_t     half             = 1;
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
  srsran_mib_nr_t mib = {};
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

  /* GNB UL initialize with configuration from phy_cfg */
  srsran_gnb_ul_t gnb_ul        = {};
  cf_t*           gnb_ul_buffer = srsran_vec_cf_malloc(sf_len);
  if (!init_gnb_ul(gnb_ul, gnb_ul_buffer, phy_cfg)) {
    logger.error("Failed to init GNB UL");
    return -1;
  }

  /* Parse command line arguments as test arguments */
  test_args_t args = parse_test_args(argc, argv);
  if (args.rnti != 0) {
    rnti = args.rnti;
  }

  /* load test dci samples */
  std::vector<cf_t> dci_samples(sf_len);
  if (args.dci_sample_filename != "") {
    dci_sample_file = args.dci_sample_filename;
  }
  if (!load_samples(dci_sample_file, dci_samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", dci_sample_file.c_str());
    return -1;
  }

  /* Extract dci slot number from the file name */
  uint32_t dci_slot_idx = parse_slot_idx_from_filename(dci_sample_file);
  for (int i = 0; i < slots_per_sf; i++) {
    /* copy samples to ue_dl processing buffer */
    srsran_vec_cf_copy(ue_dl_buffer, dci_samples.data() + slot_len * i, slot_len);
    /* Initialize slot cfg */
    srsran_slot_cfg_t slot_cfg = {.idx = dci_slot_idx + i};
    /* run ue_dl estimate fft */
    srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);

    char filename[64];
    sprintf(filename, "ofdm_dci_%d_fft%u", i, nof_sc);
    write_record_to_file(ue_dl.sf_symbols[0], nof_re, filename);

    /* search for dci */
    ue_dl_dci_search(ue_dl, phy_cfg, slot_cfg, rnti, rnti_type, phy_state, logger);
  }

  /* load test samples */
  std::vector<cf_t> samples(sf_len);
  if (args.sample_filename != "") {
    sample_file = args.sample_filename;
  }
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  if (!args.last_sample_filename.empty()) {
    last_sample_file = args.last_sample_filename;
  }
  /* load last samples from last subframe */
  std::vector<cf_t> last_samples(sf_len);
  if (half == 0) {
    if (!load_samples(last_sample_file, last_samples.data(), sf_len)) {
      logger.error("Failed to load data from %s", last_sample_file.c_str());
      return -1;
    }
  }

  /* Retrieve the slot index from file name */
  uint32_t slot_idx_pusch = parse_slot_idx_from_filename(sample_file);
  if (args.sample_filename != "") {
    half           = args.half;
    slot_idx_pusch = args.slot_idx;
  }

  /* Get the slot cfg for pusch */
  srsran_slot_cfg_t slot_cfg = {.idx = slot_idx_pusch + half};
  if (args.delay != 0) {
    ul_sample_offset = args.delay;
  }

  /* get uplink grant */
  uint32_t            pid       = 0;
  srsran_sch_cfg_nr_t pusch_cfg = {};
  if (!phy_state.get_ul_pending_grant(slot_cfg.idx, pusch_cfg, pid)) {
    logger.error("No uplink grant available");
    return -1;
  }

  /* copy samples to gnb_ul processing buffer */
  if (half == 0 && ul_sample_offset > 0) {
    /* Copy the last samples to current buffer */
    srsran_vec_cf_copy(gnb_ul_buffer, last_samples.data() + sf_len - ul_sample_offset, ul_sample_offset);
    /* Copy the remaining samples from the sample file */
    srsran_vec_cf_copy(gnb_ul_buffer + ul_sample_offset, samples.data(), slot_len - ul_sample_offset);
  } else {
    /* Copy the samples to the buffer */
    srsran_vec_cf_copy(gnb_ul_buffer, samples.data() + half * slot_len - ul_sample_offset, slot_len);
  }

  /* run gnb_ul estimate fft */
  if (srsran_gnb_ul_fft(&gnb_ul)) {
    logger.error("Error running srsran_gnb_ul_fft");
    return -1;
  }
#if ENABLE_CUDA
  FFTProcessor fft_processor(
      config.sample_rate, gnb_ul.carrier.ul_center_frequency_hz, gnb_ul.carrier.scs, &gnb_ul.fft);
  fft_processor.process_samples(gnb_ul_buffer, gnb_ul.sf_symbols[0], slot_cfg.idx);
#endif // ENABLE_CUDA

  /* Write OFDM symbols to file for debug purpose */
  char filename[64];
  sprintf(filename, "ofdm_pusch_%u_fft%u", ul_sample_offset, nof_sc);
  write_record_to_file(gnb_ul.sf_symbols[0], nof_re, filename);

  /* Apply the cfo to the signal with magic number */
  if (args.cfo != 0) {
    uplink_cfo = args.cfo;
  }
  srsran_vec_apply_cfo(gnb_ul.sf_symbols[0], uplink_cfo, gnb_ul.sf_symbols[0], nof_re);

  /* Initialize the buffer for output*/
  srsran::unique_byte_buffer_t data = srsran::make_byte_buffer();
  if (data == nullptr) {
    logger.error("Error creating byte buffer");
    return -1;
  }
  data->N_bytes = pusch_cfg.grant.tb[0].tbs / 8U;

  /* Initialize pusch result*/
  srsran_pusch_res_nr_t pusch_res      = {};
  pusch_res.tb[0].payload              = data->msg;
  srsran_softbuffer_rx_t softbuffer_rx = {};
  if (srsran_softbuffer_rx_init_guru(&softbuffer_rx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) !=
      0) {
    logger.error("Couldn't allocate and/or initialize softbuffer");
    return -1;
  }

  /* Decode PUSCH */
  if (!gnb_ul_pusch_decode(gnb_ul, pusch_cfg, slot_cfg, pusch_res, softbuffer_rx, logger)) {
    logger.error("Error running gnb_ul_pusch_decode");
    return -1;
  }

  /* if the message is not decoded correctly, then return */
  if (!pusch_res.tb[0].crc) {
    logger.debug("Error PUSCH got wrong CRC");
    return -1;
  } else {
    logger.info("PUSCH CRC passed");
  }

  /* Initialize wdissector for packet summary */
  WDWorker*                        wd_worker = new WDWorker(config.duplex_mode, config.bc_worker_log_level);
  SafeQueue<std::vector<uint8_t> > dl_msg_queue;
  SafeQueue<std::vector<uint8_t> > ul_msg_queue;
  DummyExploit*                    exploit = new DummyExploit(dl_msg_queue, ul_msg_queue);
  wd_worker->process(data->msg, data->N_bytes, rnti, 0, 0, 0, UL, exploit);
  return 0;
}