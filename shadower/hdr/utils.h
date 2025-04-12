#ifndef UTILS_H
#define UTILS_H
#include "shadower/hdr/arg_parser.h"
#include "shadower/hdr/constants.h"
#include "srsran/adt/circular_map.h"
#include "srsran/asn1/rrc_nr.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/config.h"
#include "srsran/srslog/srslog.h"
#include "srsue/hdr/phy/nr/state.h"
#include <chrono>
#include <fstream>
#include <getopt.h>
#include <optional>
#include <thread>
/* Initialize logger */
srslog::basic_logger& srslog_init(ShadowerConfig* config = nullptr);

/* Write the IQ samples to a file so that we can use tools like matlab or spectrogram-py to debug */
void write_record_to_file(cf_t* buffer, uint32_t length, char* name = nullptr, const std::string& folder = "records");

/* Load the IQ samples from a file */
bool load_samples(const std::string& filename, cf_t* buffer, size_t nsamples);

/* Read binary form configuration dumped structure */
bool read_raw_config(const std::string& filename, uint8_t* buffer, size_t size);

/* Print the buffer as hex */
std::string vec_to_hex_str(uint8_t* buffer, size_t size);

/* Set the thread priority */
void set_thread_priority(std::thread& t, int priority);

bool enable_rt_scheduler(uint8_t use_full_time = 0);

/* Add the required UDP header for wdissector */
int add_fake_header(uint8_t*             buffer,
                    uint8_t*             data,
                    uint32_t             len,
                    uint16_t             rnti,
                    uint16_t             frame_number,
                    uint16_t             slot_number,
                    direction_t          direction,
                    srsran_duplex_mode_t duplex_mode);

/* Set rar grant */
bool set_rar_grant(uint16_t                                        rnti,
                   srsran_rnti_type_t                              rnti_type,
                   uint32_t                                        slot_idx,
                   std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS>& grant,
                   srsran::phy_cfg_nr_t&                           phy_cfg,
                   srsue::nr::state&                               phy_state,
                   srslog::basic_logger&                           logger);

/* Decode SIB1 bytes to asn1 structure */
bool parse_to_sib1(uint8_t* data, uint32_t len, asn1::rrc_nr::sib1_s& sib1);

/* Decode dl_ccch_msg_s bytes to asn1 structure */
bool parse_to_dl_ccch_msg(uint8_t* data, uint32_t len, asn1::rrc_nr::dl_ccch_msg_s& dl_ccch_msg);

/* extract cell_group struct from rrc_setup */
bool extract_cell_group_cfg(asn1::rrc_nr::dl_ccch_msg_s& dl_ccch_msg, asn1::rrc_nr::cell_group_cfg_s& cell_group);

/* Initialize phy state object */
void init_phy_state(srsue::nr::state& phy_state, uint32_t nof_prb);

/* Helper function to initialize ssb */
bool init_ssb(srsran_ssb_t&               ssb,
              double                      srate,
              double                      dl_freq,
              double                      ssb_freq,
              srsran_subcarrier_spacing_t scs,
              srsran_ssb_pattern_t        pattern,
              srsran_duplex_mode_t        duplex_mode);

/* Initialize phy cfg from shadower configuration */
void init_phy_cfg(srsran::phy_cfg_nr_t& phy_cfg, ShadowerConfig& config);

/* Calculate the RA-rnti from SIB1 configuration */
std::vector<uint16_t> get_ra_rnti_list(asn1::rrc_nr::sib1_s sib1, ShadowerConfig& config);

/* Print and compare two different buffers */
void compare_two_buffers(uint8_t* buffer1, uint32_t len1, uint8_t* buffer2, uint32_t len2);

/* Turn a buffer to hex string */
std::string buffer_to_hex_string(uint8_t* buffer, uint32_t len);

/* Parse slot index from filename */
uint32_t parse_slot_idx_from_filename(const std::string& filename);

struct test_args_t {
  std::string sample_filename      = "";
  std::string last_sample_filename = "";
  std::string dci_sample_filename  = "";
  uint32_t    slot_idx             = 0;
  uint16_t    rnti                 = 0;
  uint8_t     half                 = 0;
  int32_t     delay                = 0;
  float       cfo                  = 0.0f;
};

/* Parse the command line arguments for test */
test_args_t parse_test_args(int argc, char* argv[]);

/* Load mib configuration from file and apply to phy cfg */
bool configure_phy_cfg_from_mib(srsran::phy_cfg_nr_t& phy_cfg, std::string& filename, uint32_t ncellid);

/* Load SIB1 configuration from file and apply to phy cfg */
bool configure_phy_cfg_from_sib1(srsran::phy_cfg_nr_t& phy_cfg, std::string& filename, uint32_t nbits);

/* Load RRC setup cell configuration from file and apply to phy cfg */
bool configure_phy_cfg_from_rrc_setup(srsran::phy_cfg_nr_t& phy_cfg,
                                      std::string&          filename,
                                      uint32_t              nbits,
                                      srslog::basic_logger& logger);

/* Apply MIB configuration to phy cfg */
bool update_phy_cfg_from_mib(srsran::phy_cfg_nr_t& phy_cfg, srsran_mib_nr_t& mib, uint32_t ncellid);

/* Apply SIB1 configuration to phy cfg */
void update_phy_cfg_from_sib1(srsran::phy_cfg_nr_t& phy_cfg, asn1::rrc_nr::sib1_s& sib1);

/* Apply cell configuration to phy cfg */
bool update_phy_cfg_from_cell_cfg(srsran::phy_cfg_nr_t&                                                  phy_cfg,
                                  asn1::rrc_nr::sp_cell_cfg_s&                                           sp_cell_cfg,
                                  srsran::static_circular_map<uint32_t, srsran_pucch_nr_resource_t, 128> pucch_res_list,
                                  std::map<uint32_t, srsran_csi_rs_zp_resource_t>                        csi_rs_zp_res,
                                  std::map<uint32_t, srsran_csi_rs_nzp_resource_t>                       csi_rs_nzp_res,
                                  srslog::basic_logger&                                                  logger);

/* ue_dl related configuration and update, ue_dl decode messages send from base station to UE*/
bool init_ue_dl(srsran_ue_dl_nr_t& ue_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg);
bool update_ue_dl(srsran_ue_dl_nr_t& ue_dl, srsran::phy_cfg_nr_t& phy_cfg);

/* Run PDCCH search for every CORESET and detect DCI for both dl and ul */
void ue_dl_dci_search(srsran_ue_dl_nr_t&    ue_dl,
                      srsran::phy_cfg_nr_t& phy_cfg,
                      srsran_slot_cfg_t&    slot_cfg,
                      uint16_t              rnti,
                      srsran_rnti_type_t    rnti_type,
                      srsue::nr::state&     phy_state,
                      srslog::basic_logger& logger,
                      uint32_t              task_idx = 0);
/* Detect and decode PDSCH info bytes */
bool ue_dl_pdsch_decode(srsran_ue_dl_nr_t&      ue_dl,
                        srsran_sch_cfg_nr_t&    pdsch_cfg,
                        srsran_slot_cfg_t&      slot_cfg,
                        srsran_pdsch_res_nr_t&  pdsch_res,
                        srsran_softbuffer_rx_t& softbuffer_rx,
                        srslog::basic_logger&   logger,
                        uint32_t                task_idx = 0);

/* ue_ul related configuration and update, ue_ul encode messages send from UE to base station */
bool init_ue_ul(srsran_ue_ul_nr_t& ue_ul, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg);
bool update_ue_ul(srsran_ue_ul_nr_t& ue_ul, srsran::phy_cfg_nr_t& phy_cfg);

/* gnb_dl related configuration and update, gnb_dl encode messages send from base station to UE */
bool init_gnb_dl(srsran_gnb_dl_t& gnb_dl, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg, double srate);
bool update_gnb_dl(srsran_gnb_dl_t& gnb_dl, srsran::phy_cfg_nr_t& phy_cfg);
/* use gnb_dl to encode the message targeted to UE */
bool gnb_dl_encode(std::shared_ptr<std::vector<uint8_t> > msg,
                   srsran_gnb_dl_t&                       gnb_dl,
                   srsran_dci_cfg_nr_t&                   dci_cfg,
                   srsran::phy_cfg_nr_t&                  phy_cfg,
                   srsran_sch_cfg_nr_t&                   pdsch_cfg,
                   srsran_slot_cfg_t&                     slot_cfg,
                   uint16_t                               rnti,
                   srsran_rnti_type_t                     rnti_type,
                   srslog::basic_logger&                  logger,
                   uint32_t                               mcs                 = 2,
                   uint32_t                               nof_prb_to_allocate = 24);
/* Find a search space that contains target dci format */
bool find_search_space(srsran_search_space_t** search_space, srsran::phy_cfg_nr_t& phy_cfg);
/* Find an aggregation level to use*/
bool find_aggregation_level(srsran_dci_dl_nr_t&    dci,
                            srsran_coreset_t*      coreset,
                            srsran_search_space_t* search_space,
                            uint32_t               slot_idx,
                            uint16_t               rnti);
/* Construct the dci to send */
bool construct_dci_dl_to_send(srsran_dci_dl_nr_t&   dci_to_send,
                              srsran::phy_cfg_nr_t& phy_cfg,
                              uint32_t              slot_idx,
                              uint16_t              rnti,
                              srsran_rnti_type_t    rnti_type,
                              uint32_t              mcs,
                              uint32_t              nof_prb_to_allocate);

bool construct_dci_ul_to_send(srsran_dci_ul_nr_t&   dci_to_send,
                              srsran::phy_cfg_nr_t& phy_cfg,
                              uint32_t              slot_idx,
                              uint16_t              rnti,
                              srsran_rnti_type_t    rnti_type,
                              uint32_t              mcs,
                              uint32_t              nof_prb_to_allocate);

/* gnb_ul related configuration and update, gnb_ul decode messages send from UE to base station */
bool init_gnb_ul(srsran_gnb_ul_t& gnb_ul, cf_t* buffer, srsran::phy_cfg_nr_t& phy_cfg);
bool update_gnb_ul(srsran_gnb_ul_t& gnb_ul, srsran::phy_cfg_nr_t& phy_cfg);
/* Detect and decode PUSCH info bytes */
bool gnb_ul_pusch_decode(srsran_gnb_ul_t&        gnb_ul,
                         srsran_sch_cfg_nr_t&    pusch_cfg,
                         srsran_slot_cfg_t&      slot_cfg,
                         srsran_pusch_res_nr_t&  pusch_res,
                         srsran_softbuffer_rx_t& softbuffer_rx,
                         srslog::basic_logger&   logger,
                         uint32_t                task_idx = 0);
#endif // UTILS_H