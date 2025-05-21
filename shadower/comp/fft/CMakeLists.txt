if(USE_CUDA)
    set_source_files_properties(fft_processor.cuh PROPERTIES LANGUAGE CUDA)
    set_source_files_properties(fft_processor.cu PROPERTIES LANGUAGE CUDA)
    add_library(fft_processor fft_processor.cu fft_processor.cuh)
    target_link_libraries(fft_processor CUDA::cufft CUDA::cudart)
endif(USE_CUDA)

add_executable(fft_test tests/fft_test.cc)
target_link_libraries(fft_test utils)
if(USE_CUDA)
    target_link_libraries(fft_test fft_processor)
endif(USE_CUDA)

add_test(shd_fft_test_20
    fft_test
    -s 23.04
    -f 3427.5
    -b 3421.92
    -p 51
    -h 1
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/sib.fc32
    -I 1
)

add_test(shd_fft_test_40
    fft_test
    -s 46.08
    -f 3427.5
    -b 3413.
    -p 106
    -h 1
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/sib.fc32
    -I 1
)
