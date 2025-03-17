#include "shadower/hdr/utils.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "test_variables.h"
#include <fstream>

uint16_t           rnti      = c_rnti;
srsran_rnti_type_t rnti_type = srsran_rnti_type_c;

#if TEST_TYPE == 1
std::string dci_sample_file = "shadower/test/data/srsran/dci_10222.fc32";
std::string sample_file     = "shadower/test/data/srsran/dci_10226.fc32";
uint8_t     half            = 1;
#elif TEST_TYPE == 2
std::string dci_sample_file = "shadower/test/data/dci_11686.fc32";
std::string sample_file     = "shadower/test/data/dci_11688.fc32";
uint8_t     half            = 1;
#elif TEST_TYPE == 3
std::string dci_sample_file = "shadower/test/data/srsran-n78-40MHz/ul_dci_12662.fc32";
std::string sample_file     = "shadower/test/data/srsran-n78-40MHz/pusch_12666.fc32";
uint8_t     half            = 1;
#endif // TEST_TYPE

int main(int argc, char* argv[])
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
  if (!args.dci_sample_filename.empty()) {
    dci_sample_file = args.dci_sample_filename;
  }
  if (!load_samples(dci_sample_file, dci_samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", dci_sample_file.c_str());
    return -1;
  }
  /* Extract dci slot number from the file name */
  uint32_t dci_slot_idx = parse_slot_idx_from_filename(dci_sample_file);

  /* load test samples */
  std::vector<cf_t> samples(sf_len);
  if (!args.sample_filename.empty()) {
    sample_file = args.sample_filename;
  }
  if (!load_samples(sample_file, samples.data(), sf_len)) {
    logger.error("Failed to load data from %s", sample_file.c_str());
    return -1;
  }

  /* Retrieve the slot index from file name */
  uint32_t slot_idx_pusch = parse_slot_idx_from_filename(sample_file);
  if (!args.sample_filename.empty()) {
    half           = args.half;
    slot_idx_pusch = args.slot_idx;
  }

  /* load last samples from last subframe */
  std::vector<cf_t> last_samples(sf_len);
  if (half == 0) {
    if (!load_samples(args.last_sample_filename, last_samples.data(), sf_len)) {
      logger.error("Failed to load data from %s", args.last_sample_filename.c_str());
      return -1;
    }
  }

  /* Pre-initialize softbuffer rx */
  srsran_softbuffer_rx_t softbuffer_rx = {};
  if (srsran_softbuffer_rx_init_guru(&softbuffer_rx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) !=
      0) {
    logger.error("Couldn't allocate and/or initialize softbuffer");
    return -1;
  }

  uint32_t max_advancement = 0;
  double   max_snr         = 0;
  for (uint32_t symbol_in_last_slot = ul_sample_offset - 10; symbol_in_last_slot < ul_sample_offset + 10;
       symbol_in_last_slot++) {
    /* Run dci search first */
    for (int i = 0; i < slots_per_sf; i++) {
      /* copy samples to ue_dl processing buffer */
      srsran_vec_cf_copy(ue_dl_buffer, dci_samples.data() + slot_len * i, slot_len);
      /* Initialize slot cfg */
      srsran_slot_cfg_t slot_cfg = {.idx = dci_slot_idx + i};
      /* run ue_dl estimate fft */
      srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);
      /* search for dci */
      ue_dl_dci_search(ue_dl, phy_cfg, slot_cfg, rnti, rnti_type, phy_state, logger);
    }

    /* Get the slot cfg for pusch */
    srsran_slot_cfg_t slot_cfg = {.idx = slot_idx_pusch + half};

    /* get uplink grant */
    uint32_t            pid       = 0;
    srsran_sch_cfg_nr_t pusch_cfg = {};
    if (!phy_state.get_ul_pending_grant(slot_cfg.idx, pusch_cfg, pid)) {
      logger.error("No uplink grant available");
      return -1;
    }

    /* copy samples to gnb_ul processing buffer */
    if (half == 0 && symbol_in_last_slot > 0) {
      /* Copy the last samples to current buffer */
      srsran_vec_cf_copy(gnb_ul_buffer, last_samples.data() + sf_len - symbol_in_last_slot, symbol_in_last_slot);
      /* Copy the remaining samples from the sample file */
      srsran_vec_cf_copy(gnb_ul_buffer + symbol_in_last_slot, samples.data(), slot_len - symbol_in_last_slot);
    } else {
      /* Copy the samples to the buffer */
      srsran_vec_cf_copy(gnb_ul_buffer, samples.data() + half * slot_len - symbol_in_last_slot, slot_len);
    }

    /* run gnb_ul estimate fft */
    if (srsran_gnb_ul_fft(&gnb_ul)) {
      logger.error("Error running srsran_gnb_ul_fft");
      return -1;
    }

    /* Run pusch channel estimation */
    if (srsran_dmrs_sch_estimate(
            &gnb_ul.dmrs, &slot_cfg, &pusch_cfg, &pusch_cfg.grant, gnb_ul.sf_symbols[0], &gnb_ul.chest_pusch)) {
      logger.error("Error running srsran_dmrs_sch_estimate");
      return -1;
    }

    if (gnb_ul.chest_pusch.snr_db < 1) {
      continue;
    }

    if (gnb_ul.chest_pusch.snr_db > max_snr) {
      max_snr         = gnb_ul.chest_pusch.snr_db;
      max_advancement = symbol_in_last_slot;
    }
  }
  logger.info("Max SNR: %f, Max Advancement: %d", max_snr, max_advancement);
  return 0;
}