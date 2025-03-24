#include "shadower/hdr/utils.h"
#include "shadower/hdr/wd_worker.h"
#include "shadower/test/dummy_exploit.h"
#include "srsran/asn1/rrc_nr.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "test_variables.h"
#if ENABLE_CUDA
#include "shadower/hdr/fft_processor.cuh"
#endif // ENABLE_CUDA
uint16_t           rnti      = c_rnti;
srsran_rnti_type_t rnti_type = srsran_rnti_type_c;

#if TEST_TYPE == 1
// std::string sample_file = "shadower/test/data/srsran-n78-20MHz/pdsch_3440.fc32";
// uint8_t     half        = 0;
// std::string sample_file = "shadower/test/data/srsran-n78-20MHz/pdsch_3640.fc32";
// uint8_t     half        = 1;
std::string sample_file = "shadower/test/data/srsran-n78-20MHz/pdsch_3684.fc32";
uint8_t     half        = 1;
#elif TEST_TYPE == 2
std::string sample_file = "/root/overshadow/effnet/sf_152_11864.fc32";
uint8_t     half        = 1;
#elif TEST_TYPE == 3
// std::string sample_file = "shadower/test/data/srsran-n78-40MHz/pdsch_13640.fc32";
// uint8_t     half        = 0;
// std::string sample_file = "shadower/test/data/srsran-n78-40MHz/pdsch_13720.fc32";
// uint8_t     half        = 0;
std::string sample_file = "shadower/test/data/srsran-n78-40MHz/pdsch_13726.fc32";
uint8_t     half        = 0;
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

  /* Retrieve the slot index from file name */
  uint32_t slot_number = parse_slot_idx_from_filename(sample_file);
  if (!args.sample_filename.empty()) {
    half        = args.half;
    slot_number = args.slot_idx;
  }

  /* copy samples to ue_dl processing buffer */
  srsran_vec_cf_copy(ue_dl_buffer, samples.data() + slot_len * half + args.delay, slot_len);
  /* Initialize slot cfg */
  srsran_slot_cfg_t slot_cfg = {.idx = slot_number + half};
  /* run ue_dl estimate fft */
  srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);

  /* Write OFDM symbols to file for debug purpose */
  char filename[64];
  sprintf(filename, "ofdm_pdsch_fft%u", nof_sc);
  write_record_to_file(ue_dl.sf_symbols[0], nof_re, filename);

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

  /* run ue_dl estimate fft on corrected buffer */
  srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);
#if ENABLE_CUDA
  FFTProcessor fft_processor(
      config.sample_rate, ue_dl.carrier.dl_center_frequency_hz, ue_dl.carrier.scs, &ue_dl.fft[0]);
  fft_processor.to_ofdm(ue_dl_buffer, ue_dl.sf_symbols[0], slot_cfg.idx);
#endif // ENABLE_CUDA

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

  /* Print received messages bytes in hex */
  std::ostringstream oss;
  for (uint32_t i = 0; i < data->N_bytes; i++) {
    oss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(data->msg[i]) << ", ";
  }
  logger.info("Decoded message: %s", oss.str().c_str());

  /* Run wdissector for packet summary */
  WDWorker*                        wd_worker = new WDWorker(config.duplex_mode, config.bc_worker_log_level);
  SafeQueue<std::vector<uint8_t> > dl_msg_queue;
  SafeQueue<std::vector<uint8_t> > ul_msg_queue;
  DummyExploit*                    exploit = new DummyExploit(dl_msg_queue, ul_msg_queue);
  wd_worker->process(data->msg, data->N_bytes, rnti, 0, 0, slot_cfg.idx, DL, exploit);

  /* Decode as MAC PDU*/
  srsran::mac_sch_pdu_nr pdu;
  if (pdu.unpack(data->msg, data->N_bytes) != SRSRAN_SUCCESS) {
    logger.error("Failed to unpack MAC SDU");
    return -1;
  }

  uint32_t num_pdu = pdu.get_num_subpdus();
  for (uint32_t i = 0; i < pdu.get_num_subpdus(); i++) {
    srsran::mac_sch_subpdu_nr& subpdu = pdu.get_subpdu(i);
    logger.info("LCID: %u length: %u", subpdu.get_lcid(), subpdu.get_sdu_length());

    switch (subpdu.get_lcid()) {
      case srsran::mac_sch_subpdu_nr::nr_lcid_sch_t::CCCH: {
        asn1::rrc_nr::dl_ccch_msg_s dl_ccch_msg;
        if (!parse_to_dl_ccch_msg(subpdu.get_sdu(), subpdu.get_sdu_length(), dl_ccch_msg)) {
          logger.error("Failed to parse DL-CCCH message");
          return -1;
        }
        asn1::json_writer json_writer;
        dl_ccch_msg.msg.to_json(json_writer);
        logger.debug("CCCH message: %s", json_writer.to_string().c_str());
        break;
      }
      case srsran::mac_sch_subpdu_nr::nr_lcid_sch_t::CON_RES_ID: {
        srsran::mac_sch_subpdu_nr::ue_con_res_id_t con_res_id = subpdu.get_ue_con_res_id_ce();
        std::ostringstream                         oss;
        for (uint32_t i = 0; i < srsran::mac_sch_subpdu_nr::ue_con_res_id_len; i++) {
          oss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(con_res_id.data()[i]);
        }
        logger.info("Contention resolution ID: %s", oss.str().c_str());
      }
      case 1: {
        uint8_t* sdu     = subpdu.get_sdu();
        uint32_t sdu_len = subpdu.get_sdu_length();
        uint8_t* am_data = sdu;
        if (sdu[0] & 0x80) {
          am_data += 2; /* AM header */
          am_data += 2; /* PDCP header */
          sdu_len -= 4;
        } else {
          logger.info("ACK sn");
          continue;
        }
        /* Decode the message to DL_DCCH_msg*/
        asn1::rrc_nr::dl_dcch_msg_s dl_dcch_msg;
        asn1::cbit_ref              bref(am_data, sdu_len);
        asn1::SRSASN_CODE           err = dl_dcch_msg.unpack(bref);
        if (err != asn1::SRSASN_SUCCESS) {
          logger.error("Error unpacking DL-DCCH message");
          return -1;
        }
        /* rrc reconfiguration */
        if (dl_dcch_msg.msg.type().value == asn1::rrc_nr::dl_dcch_msg_type_c::types_opts::c1) {
          if (dl_dcch_msg.msg.c1().type() == asn1::rrc_nr::dl_dcch_msg_type_c::c1_c_::types::rrc_recfg) {
            asn1::rrc_nr::rrc_recfg_s& rrc_recfg = dl_dcch_msg.msg.c1().rrc_recfg();
            if (rrc_recfg.crit_exts.rrc_recfg().non_crit_ext_present) {
              asn1::rrc_nr::cell_group_cfg_s cell_group_cfg;
              asn1::cbit_ref bref_cg(rrc_recfg.crit_exts.rrc_recfg().non_crit_ext.master_cell_group.data(),
                                     rrc_recfg.crit_exts.rrc_recfg().non_crit_ext.master_cell_group.size());
              if (cell_group_cfg.unpack(bref_cg) != asn1::SRSASN_SUCCESS) {
                logger.error("Could not unpack master cell group config");
                return -1;
              }
              asn1::json_writer json_writer;
              cell_group_cfg.to_json(json_writer);
              logger.info("RRC Reconfiguration: %s", json_writer.to_string().c_str());
              std::ofstream file{"logs/rrc_reconfiguration.json"};
              file << json_writer.to_string();
              return -1;
            }
          }
        }
        break;
      }
      default:
        break;
    }
  }
  return 0;
}