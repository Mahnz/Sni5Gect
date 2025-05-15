# File source to read directly from the files
add_library(file_source SHARED ${SHADOWER_DIR}/source/file_source.cc ${SHADOWER_DIR}/utils/utils.cc)

# UHD source to use the USRP device
add_library(uhd_source SHARED ${SHADOWER_DIR}/source/uhd_source.cc)
target_link_libraries(uhd_source srsran_phy srsran_rf)