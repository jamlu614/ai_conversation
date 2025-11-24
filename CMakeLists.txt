cmake_minimum_required(VERSION 3.15)
project(ai_conversation)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -std=gnu11 ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=gnu++11 ")

# 设置默认构建类型为Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# include(${CMAKE_SOURCE_DIR}/scripts/env.cmake)
# include(${CMAKE_SOURCE_DIR}/scripts/os.cmake)
# include(${CMAKE_SOURCE_DIR}/scripts/check.cmake)

if("${MEM_CHECK}" STREQUAL "true")
    set(sanitizer -fsanitize=address)
endif()

set(AGORA_MEDIA_SDK_DIR ${CMAKE_SOURCE_DIR}/agora_sdk)
set(AGORA_MEDIA_SDK_HEADER_DIR ${CMAKE_SOURCE_DIR}/agora_sdk/include)
set(FFMPEG_SDK_DIR ${CMAKE_SOURCE_DIR}/ffmpeg)


set(CMAKE_EXE_LINKER_FLAGS "-Wl,--disable-new-dtags,-rpath=$ORIGIN/agora_sdk:$ORIGIN/ffmpeg/lib")
# set(CMAKE_EXE_LINKER_FLAGS "-Wl,--disable-new-dtags,-rpath=\\\$$ORIGIN/agora_sdk:\\\$$ORIGIN/ffmpeg/lib")

message(STATUS "run path:${CMAKE_EXE_LINKER_FLAGS}")

# Set common flags
set(CMAKE_DEBUG_POSTFIX "")
set(CMAKE_RELEASE_POSTFIX "")
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -Wno-unused-command-line-argument ${sanitizer}")

if("${LICENSE_CHECK}" STREQUAL "y")
    message("license check is enabled")
    add_definitions(-DLICENSE_CHECK)
endif()

# include header path
# include_directories(
#     "${FFMPEG_SDK_DIR}/include"
#    " ${CMAKE_SOURCE_DIR}/common"
#     "${CMAKE_SOURCE_DIR}/common/file_parser"
#     "${THIRD_PARTY}/include"
#     "${THIRD_PARTY}/http_parser/include"
#     "${THIRD_PARTY}/json_parser/include"
#     "${THIRD_PARTY}/opusfile_parser/include"
#     "${CMAKE_SOURCE_DIR}"
#     "${AGORA_MEDIA_SDK_HEADER_DIR}")

# link_directories(
#     ${AGORA_MEDIA_SDK_DIR}
#     ${FFMPEG_SDK_DIR}/lib
#     )

file(GLOB SAMPLE_SEND_H264_PCM_CPP_FILES
    "${CMAKE_SOURCE_DIR}/common/*.cpp")

file(GLOB SRC_FILE "${CMAKE_SOURCE_DIR}/*.cpp" "${CMAKE_SOURCE_DIR}/*.c")


add_executable(${CMAKE_PROJECT_NAME} ${SRC_FILE} ${SAMPLE_SEND_H264_PCM_CPP_FILES})

# # 设置 RPATH 为相对路径（$ORIGIN 表示可执行文件所在目录）
# set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
#   INSTALL_RPATH "\$ORIGIN/agora_sdk:\$ORIGIN/ffmpeg/lib"
#     # BUILD_WITH_INSTALL_RPATH TRUE
# )

# install(TARGETS ${CMAKE_PROJECT_NAME} DESTINATION bin)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${FFMPEG_SDK_DIR}/include"
   " ${CMAKE_SOURCE_DIR}/common"
    "${CMAKE_SOURCE_DIR}/common/file_parser"
    "${THIRD_PARTY}/include"
    "${THIRD_PARTY}/http_parser/include"
    "${THIRD_PARTY}/json_parser/include"
    "${THIRD_PARTY}/opusfile_parser/include"
    "${CMAKE_SOURCE_DIR}"
    "${AGORA_MEDIA_SDK_HEADER_DIR}")

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE 
${AGORA_MEDIA_SDK_DIR} 
${FFMPEG_SDK_DIR}/lib
)


target_link_libraries(
    ${CMAKE_PROJECT_NAME}

    # -Wl,--start-group
    avformat
    avdevice
    avcodec
    avutil
    avfilter
    swresample
    swscale
    rockchip_mpp
    # drm
    # -Wl,--end-group
    asound
    pthread
    # x264
    # v4l2
    z
    crypto
    agora_rtc_sdk
    agora_rtm_sdk
    agora-core
    agora-fdkaac
    agora-ffmpeg
    agora-soundtouch
    yuv
    SDL2-2.0
    curl
    mosquitto
    inih
    speexdsp
)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:-g;-O0>
)

