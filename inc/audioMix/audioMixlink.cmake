# audioMixlink用于将audioMix实现的源码编译到指定程序中
# 使用者在需要使用hybird时引入此文件并将AUDIOMIX加入到add_executable中即可

set(AUDIOMIX_DIR "${PROJECT_SOURCE_DIR}/inc/audioMix/core")
aux_source_directory(${AUDIOMIX_DIR} AUDIOMIX)