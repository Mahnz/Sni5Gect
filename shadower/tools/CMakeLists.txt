add_executable(recorder ${PROJECT_SOURCE_DIR}/shadower/tools/recorder.cc ${PROJECT_SOURCE_DIR}/shadower/src/source.cc)
target_link_libraries(recorder pthread liquid)