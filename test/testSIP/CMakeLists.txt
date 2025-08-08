set(TARGET_NAME "testSIP")
#aux_source_directory("${PROJECT_SOURCE_DIR}/test/testSIP" CPP_LIST)

add_executable(${TARGET_NAME} "test.cpp")
#add_executable(${TARGET_NAME} "simple-pjsip.cpp")

target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDE})
target_link_libraries(${TARGET_NAME} PRIVATE ${PROJECT_LIBS})