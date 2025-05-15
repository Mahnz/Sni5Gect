add_executable(recorder ${SHADOWER_DIR}/tools/recorder.cc ${SHADOWER_DIR}/source/source.cc)
target_link_libraries(recorder pthread liquid)

add_executable(cutter ${SHADOWER_DIR}/tools/cutter.cc)