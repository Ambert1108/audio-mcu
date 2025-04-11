set(TARGET_NAME "faust.${PROJECT_VERSION}")
aux_source_directory("${PROJECT_SOURCE_DIR}/src" CPP_LIST)
add_executable(${TARGET_NAME} ${CPP_LIST} ${REMIX})
target_compile_definitions(${TARGET_NAME} PRIVATE USE_X_MIX=0)

target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDE})
target_link_libraries(${TARGET_NAME} PRIVATE ${PROJECT_LIBS})

file(COPY "${PROJECT_SOURCE_DIR}/res/config.ini" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/resources/")