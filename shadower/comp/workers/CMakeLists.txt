add_library(broadcast_worker broadcast_worker.cc broadcast_worker.h)
target_link_libraries(broadcast_worker utils srsran_mac)
if(USE_CUDA)
    target_link_libraries(broadcast_worker fft_processor)
endif(USE_CUDA)

add_executable(broadcast_worker_sib1_test tests/broadcast_worker_sib1_test.cc)
target_link_libraries(broadcast_worker_sib1_test broadcast_worker utils)

add_test(shd_broadcast_worker_sib1_test_20MHz
    broadcast_worker_sib1_test -s 23.04 -f 3427.5 -b 3421.92 -i 0 -I 1 -p 51
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/sib.fc32
    -m ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/mib.raw
)

add_test(shd_broadcast_worker_sib1_test_40MHz
    broadcast_worker_sib1_test -s 46.08 -f 3427.5 -b 3413.28 -i 0 -I 1 -p 106
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/sib.fc32
    -m ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/mib.raw
)

add_executable(broadcast_worker_rar_test tests/broadcast_worker_rar_test.cc)
target_link_libraries(broadcast_worker_rar_test broadcast_worker utils)

add_test(shd_broadcast_worker_rar_test_20MHz
    broadcast_worker_rar_test -s 23.04 -f 3427.5 -b 3421.92 -i 10 -I 1 -p 51 -R 267
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/rar.fc32
    -z ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/sib1.raw -Z 101
    -m ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-20MHz/mib.raw
)

add_test(shd_broadcast_worker_rar_test_40MHz
    broadcast_worker_rar_test -s 46.08 -f 3427.5 -b 3413.28 -i 10 -I 1 -p 106 -R 267
    -F ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/rar.fc32
    -z ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/sib1.raw -Z 101
    -m ${CMAKE_SOURCE_DIR}/shadower/test/data/srsran-n78-40MHz/mib.raw
)

add_library(wd_worker wd_worker.cc)
target_link_libraries(wd_worker wdissector srslog)
target_include_directories(wd_worker PUBLIC ${WDISSECTOR_PATH}/src ${WDISSECTOR_INCLUDES})

add_executable(wd_worker_test tests/wd_worker_test.cc)
target_link_libraries(wd_worker_test wd_worker utils)
