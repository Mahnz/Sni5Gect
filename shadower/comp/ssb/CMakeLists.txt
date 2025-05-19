if(USE_CUDA)
    set_source_files_properties(ssb_cuda.cuh PROPERTIES LANGUAGE CUDA)
    set_source_files_properties(ssb_cuda.cu PROPERTIES LANGUAGE CUDA)
    add_library(ssb_cuda ssb_cuda.cu ssb_cuda.cuh)
    target_link_libraries(ssb_cuda CUDA::cufft CUDA::cudart)

    add_executable(ssb_cuda_test tests/ssb_cuda_test.cc)
    target_link_libraries(ssb_cuda_test ssb_cuda utils)

    add_custom_target(run_ssb_test_20
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/ssb_cuda_test
        -s 23.04
        -f 3427.5
        -b 3421.92
        -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/sib.fc32
        -I 1
        DEPENDS ssb_cuda_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running ssb_cuda_test with 20MHz bandwidth"
    )

    add_custom_target(run_ssb_test_40
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/ssb_cuda_test
        -s 46.08
        -f 3427.5
        -b 3413.28
        -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/sib.fc32
        -I 1
        DEPENDS ssb_cuda_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running ssb_cuda_test with 40MHz bandwidth"
    )
endif(USE_CUDA)
