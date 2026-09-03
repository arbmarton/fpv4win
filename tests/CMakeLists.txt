# Decoder / renderer tests. Enable with -DFPV4WIN_BUILD_TESTS=ON, then run tests/run_tests.sh.
set(PLAYER_SRC_DIR ${CMAKE_SOURCE_DIR}/src/player)

add_executable(test_decoder test_decoder.cpp ${PLAYER_SRC_DIR}/ffmpegDecode.cpp)
target_include_directories(test_decoder PRIVATE ${PLAYER_SRC_DIR})
target_link_libraries(test_decoder Qt5::Core ${FFMPEG_LIBRARIES})

add_executable(test_render test_render.cpp
        ${PLAYER_SRC_DIR}/QQuickRealTimePlayer.cpp
        ${PLAYER_SRC_DIR}/QQuickRealTimePlayer.h
        ${PLAYER_SRC_DIR}/RealTimeRenderer.cpp
        ${PLAYER_SRC_DIR}/ffmpegDecode.cpp
        ${PLAYER_SRC_DIR}/JpegEncoder.cpp
        ${PLAYER_SRC_DIR}/GifEncoder.cpp
        ${PLAYER_SRC_DIR}/Mp4Encoder.cpp)
target_include_directories(test_render PRIVATE ${CMAKE_SOURCE_DIR}/src ${PLAYER_SRC_DIR})
target_link_libraries(test_render Qt5::Quick ${FFMPEG_LIBRARIES} PkgConfig::SDL2)
