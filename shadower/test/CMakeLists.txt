set(TestCommonSources ${SHADOWER_DIR}/utils.cc ${SHADOWER_DIR}/trace_samples.cc)

macro(add_shd_test exec_name)
  set(EXE_PATH ${CMAKE_BINARY_DIR}/shadower/test/${exec_name})

  set(TEST_NAME_0 "shd_${exec_name}_srsran_n78_20MHz")
  add_test(NAME ${TEST_NAME_0} COMMAND ${EXE_PATH} 0)
  set_tests_properties(${TEST_NAME_0} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  message("Adding test ${TEST_NAME_0}")

  set(TEST_NAME_1 "shd_${exec_name}_srsran_n78_40MHz")
  add_test(NAME ${TEST_NAME_1} COMMAND ${EXE_PATH} 1)
  set_tests_properties(${TEST_NAME_1} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  message("Adding test ${TEST_NAME_1}")
endmacro()

add_executable(cell_search_test cell_search_test.cc)
target_link_libraries(cell_search_test utils)
if(USE_CUDA)
  target_link_libraries(cell_search_test ssb_cuda)
endif(USE_CUDA)
add_shd_test(cell_search_test)

add_executable(sib_search_test sib_search_test.cc)
target_link_libraries(sib_search_test utils)
if(USE_CUDA)
  target_link_libraries(sib_search_test fft_processor)
endif(USE_CUDA)
add_shd_test(sib_search_test)

add_executable(rar_search_test rar_search_test.cc)
target_link_libraries(rar_search_test utils)
add_shd_test(rar_search_test)

add_executable(rrc_setup_test rrc_setup_test.cc ${TestCommonSources})
target_link_libraries(rrc_setup_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_executable(contention_resolution_test contention_resolution_test.cc ${TestCommonSources})
target_link_libraries(contention_resolution_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_executable(pdsch_decode_test pdsch_decode_test.cc ${TestCommonSources} ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(pdsch_decode_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)
if (USE_CUDA)
  target_link_libraries(pdsch_decode_test fft_processor)
endif(USE_CUDA)

add_executable(pdsch_decode_search pdsch_decode_search.cc ${TestCommonSources} ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(pdsch_decode_search ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

target_link_libraries(broadcast_worker_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_executable(rach_msg3_test rach_msg3_test.cc ${TestCommonSources})
target_link_libraries(rach_msg3_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_executable(pusch_decode_test pusch_decode_test.cc ${TestCommonSources} ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(pusch_decode_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)
if (USE_CUDA)
  target_link_libraries(pusch_decode_test fft_processor)
endif(USE_CUDA)

add_executable(pusch_cfo_test pusch_cfo_test.cc ${TestCommonSources} ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(pusch_cfo_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

add_executable(pdsch_encode_test pdsch_encode_test.cc ${TestCommonSources} ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(pdsch_encode_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

add_executable(dci_ul_encode_test dci_ul_encode_test.cc ${TestCommonSources})
target_link_libraries(dci_ul_encode_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_executable(msg_helper_test msg_helper_test.cc ${TestCommonSources} ${SHADOWER_DIR}/msg_helper.cc ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(msg_helper_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

add_executable(sib_overshadow_test sib_overshadow_test.cc ${TestCommonSources}
    ${SHADOWER_DIR}/syncer.cc
    ${SHADOWER_DIR}/ring_buffer.cc
)
target_link_libraries(sib_overshadow_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} zmq)
if (USE_CUDA)
  target_link_libraries(sib_overshadow_test ssb_cuda)
endif(USE_CUDA)

add_executable(sdr_timing_test sdr_timing_test.cc ${TestCommonSources} ${SHADOWER_DIR}/syncer.cc)
target_link_libraries(sdr_timing_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES})
if (USE_CUDA)
  target_link_libraries(sdr_timing_test ssb_cuda)
endif(USE_CUDA)

add_executable(exploit_rrc_setup_test exploit_rrc_setup_test.cc ${TestCommonSources} ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(exploit_rrc_setup_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

add_executable(rrc_release_construction rrc_release_construction.cc ${TestCommonSources} ${SHADOWER_DIR}/msg_helper.cc ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(rrc_release_construction ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

add_executable(security_mode_command_construction security_mode_command_construction.cc ${TestCommonSources} ${SHADOWER_DIR}/msg_helper.cc ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(security_mode_command_construction ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

add_executable(registration_accept_constructor registration_accept_constructor.cc ${TestCommonSources} ${SHADOWER_DIR}/msg_helper.cc ${SHADOWER_DIR}/wd_worker.cc)
target_link_libraries(registration_accept_constructor ${SRSUE_SOURCES} ${SRSRAN_SOURCES} wdissector)

if (USE_CUDA)
    add_executable(fft_test fft_test.cc ${SHADOWER_DIR}/fft_processor.cu ${TestCommonSources})
    target_link_libraries(fft_test ${SRSUE_SOURCES} ${SRSRAN_SOURCES} ${FFT_LIBRARIES} fft_processor)
endif(USE_CUDA)

add_executable(ldpc_test ldpc_test.cc)
target_link_libraries(ldpc_test PRIVATE ${SRSUE_SOURCES} ${SRSRAN_SOURCES} srsran_phch srsran_fec)

add_executable(pcap_writer pcap_writer.cc)
target_link_libraries(pcap_writer ${SRSUE_SOURCES} ${SRSRAN_SOURCES})

add_executable(resampler_compare resampler_compare.cc)
target_link_libraries(resampler_compare ${SRSUE_SOURCES} ${SRSRAN_SOURCES} liquid)