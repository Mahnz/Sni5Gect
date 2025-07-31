add_executable(cell_search_test cell_search_test.cc)
target_link_libraries(cell_search_test utils)
if(ENABLE_CUDA)
  target_link_libraries(cell_search_test ssb_cuda)
endif(ENABLE_CUDA)
add_test(shd_cell_search_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/cell_search_test 0)
set_tests_properties(shd_cell_search_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(shd_cell_search_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/cell_search_test 1)
set_tests_properties(shd_cell_search_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(sib_search_test sib_search_test.cc)
target_link_libraries(sib_search_test utils)
if(ENABLE_CUDA)
  target_link_libraries(sib_search_test fft_processor)
endif(ENABLE_CUDA)
add_test(shd_sib_search_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/sib_search_test 0)
set_tests_properties(shd_sib_search_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(shd_sib_search_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/sib_search_test 1)
set_tests_properties(shd_sib_search_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(rar_search_test rar_search_test.cc)
target_link_libraries(rar_search_test utils)
add_test(shd_rar_search_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/rar_search_test 0)
set_tests_properties(shd_rar_search_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(shd_rar_search_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/rar_search_test 1)
set_tests_properties(shd_rar_search_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(contention_resolution_test contention_resolution_test.cc)
target_link_libraries(contention_resolution_test utils)
add_test(shd_contention_resolution_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/contention_resolution_test 0)
set_tests_properties(shd_contention_resolution_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(shd_contention_resolution_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/contention_resolution_test 1)
set_tests_properties(shd_contention_resolution_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(rach_msg3_test rach_msg3_test.cc)
target_link_libraries(rach_msg3_test utils)
add_test(shd_rach_msg3_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/rach_msg3_test 0)
set_tests_properties(shd_rach_msg3_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(shd_rach_msg3_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/rach_msg3_test 1)
set_tests_properties(shd_rach_msg3_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(rrc_setup_test rrc_setup_test.cc)
target_link_libraries(rrc_setup_test utils)
add_test(shd_rrc_setup_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/rrc_setup_test 0)
set_tests_properties(shd_rrc_setup_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_test(shd_rrc_setup_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/rrc_setup_test 1)
set_tests_properties(shd_rrc_setup_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(pdsch_decode_test pdsch_decode_test.cc)
target_link_libraries(pdsch_decode_test utils wd_worker)

add_test(shd_pdsch_decode_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 0)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pdsch_decode_test_srsran_n78_20MHz_0 ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 0 -f ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/pdsch_3440.fc32 -s 0 -h 0)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_20MHz_0 PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pdsch_decode_test_srsran_n78_20MHz_1 ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 0 -f ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/pdsch_3640.fc32 -s 0 -h 1)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_20MHz_1 PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pdsch_decode_test_srsran_n78_20MHz_2 ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 0 -f ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/pdsch_3684.fc32 -s 4 -h 1)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_20MHz_2 PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pdsch_decode_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 1)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pdsch_decode_test_srsran_n78_40MHz_0 ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 1 -f ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/pdsch_13720.fc32 -s 0 -h 0)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_40MHz_0 PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pdsch_decode_test_srsran_n78_40MHz_1 ${CMAKE_BINARY_DIR}/shadower/test/pdsch_decode_test 1 -f ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/pdsch_13726.fc32 -s 6 -h 0)
set_tests_properties(shd_pdsch_decode_test_srsran_n78_40MHz_1 PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(pusch_decode_test pusch_decode_test.cc)
target_link_libraries(pusch_decode_test utils wd_worker)
add_test(shd_pusch_decode_test_srsran_n78_20MHz ${CMAKE_BINARY_DIR}/shadower/test/pusch_decode_test 0)
set_tests_properties(shd_pusch_decode_test_srsran_n78_20MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pusch_decode_test_0 ${CMAKE_BINARY_DIR}/shadower/test/pusch_decode_test 0 -d ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/dci_ul_3422.fc32 -s 0 -h 1)
set_tests_properties(shd_pusch_decode_test_0 PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_test(shd_pusch_decode_test_srsran_n78_40MHz ${CMAKE_BINARY_DIR}/shadower/test/pusch_decode_test 1)
set_tests_properties(shd_pusch_decode_test_srsran_n78_40MHz PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(msg_helper_test msg_helper_test.cc)
target_link_libraries(msg_helper_test wd_worker utils)
add_test(shd_msg_helper_test ${CMAKE_BINARY_DIR}/shadower/test/msg_helper_test)
set_tests_properties(shd_msg_helper_test PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})