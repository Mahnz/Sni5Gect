add_library(syncer syncer.cc syncer.h)
target_link_libraries(syncer srsran_phy srsran_common utils)
if (ENABLE_CUDA)
    target_link_libraries(syncer ssb_cuda)
endif(ENABLE_CUDA)

add_executable(syncer_test tests/syncer_test.cc)
target_link_libraries(syncer_test syncer source)
