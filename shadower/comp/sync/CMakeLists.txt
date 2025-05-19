add_library(syncer syncer.cc syncer.h)
target_link_libraries(syncer srsran_phy srsran_common utils)
if (USE_CUDA)
    target_link_libraries(syncer ssb_cuda)
endif(USE_CUDA)

add_executable(syncer_test tests/syncer_test.cc)
target_link_libraries(syncer_test syncer source)
