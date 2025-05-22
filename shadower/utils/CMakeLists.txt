add_library(utils
    src/utils.cc
    src/ssb_utils.cc
    src/phy_cfg_utils.cc
    src/ue_dl_utils.cc
    src/gnb_ul_utils.cc
    src/dci_utils.cc
)
target_link_libraries(utils ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_library(thread_pool thread_pool.cc)
target_link_libraries(thread_pool Threads::Threads utils)
