add_library(utils src/utils.cc)
target_link_libraries(utils srslog)

add_library(ssb_utils src/ssb_utils.cc)
target_link_libraries(ssb_utils srslog srsran_phy)

add_library(phy_cfg_utils src/phy_cfg_utils.cc)
target_link_libraries(phy_cfg_utils srsran_ue srsran_common srslog)

add_library(ue_dl_utils src/ue_dl_utils.cc)
target_link_libraries(ue_dl_utils srsran_ue srsran_common srslog)
