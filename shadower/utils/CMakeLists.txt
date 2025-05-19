add_library(utils
    utils.cc
    ssb_utils.cc
)
target_link_libraries(utils srslog srsran_phy)
