#include "shadower/utils/arg_parser.h"
#include "srsran/common/phy_cfg_nr.h"

/* Initialize phy cfg from shadower configuration */
void init_phy_cfg(srsran::phy_cfg_nr_t& phy_cfg, ShadowerConfig& config);

// /* Load mib configuration from file and apply to phy cfg */
// bool configure_phy_cfg_from_mib(srsran::phy_cfg_nr_t& phy_cfg, std::string& filename, uint32_t ncellid);

// /* Load SIB1 configuration from file and apply to phy cfg */
// bool configure_phy_cfg_from_sib1(srsran::phy_cfg_nr_t& phy_cfg, std::string& filename, uint32_t nbits);

// /* Load RRC setup cell configuration from file and apply to phy cfg */
// bool configure_phy_cfg_from_rrc_setup(srsran::phy_cfg_nr_t& phy_cfg,
//                                       std::string&          filename,
//                                       uint32_t              nbits,
//                                       srslog::basic_logger& logger);

// /* Apply MIB configuration to phy cfg */
// bool update_phy_cfg_from_mib(srsran::phy_cfg_nr_t& phy_cfg, srsran_mib_nr_t& mib, uint32_t ncellid);

// /* Apply SIB1 configuration to phy cfg */
// void update_phy_cfg_from_sib1(srsran::phy_cfg_nr_t& phy_cfg, asn1::rrc_nr::sib1_s& sib1);

// /* Apply cell configuration to phy cfg */
// bool update_phy_cfg_from_cell_cfg(srsran::phy_cfg_nr_t&                                                  phy_cfg,
//                                   asn1::rrc_nr::sp_cell_cfg_s&                                           sp_cell_cfg,
//                                   srsran::static_circular_map<uint32_t, srsran_pucch_nr_resource_t, 128>
//                                   pucch_res_list, std::map<uint32_t, srsran_csi_rs_zp_resource_t> csi_rs_zp_res,
//                                   std::map<uint32_t, srsran_csi_rs_nzp_resource_t> csi_rs_nzp_res,
//                                   srslog::basic_logger&                                                  logger);