if(USE_CUDA)
    set_source_files_properties(ssb_cuda.cuh PROPERTIES LANGUAGE CUDA)
    set_source_files_properties(ssb_cuda.cu PROPERTIES LANGUAGE CUDA)
    add_library(ssb_cuda ssb_cuda.cu ssb_cuda.cuh)
    target_link_libraries(ssb_cuda CUDA::cufft CUDA::cudart)
endif(USE_CUDA)

add_executable(ssb_test tests/ssb_test.cc)
target_link_libraries(ssb_test utils)
if(USE_CUDA)
    target_link_libraries(ssb_test ssb_cuda)
endif(USE_CUDA)

add_test(shd_ssb_test_20
    ssb_test
    -s 23.04
    -f 3427.5
    -b 3421.92
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/sib.fc32
    -I 1
)

add_test(shd_ssb_test_40
    ssb_test
    -s 46.08
    -f 3427.5
    -b 3413.28
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/sib.fc32
    -I 1
)
