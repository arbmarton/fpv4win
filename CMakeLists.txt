cmake_minimum_required(VERSION 3.20)
project(fpv4win)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

unset(Qt6_DIR CACHE)
unset(Qt6Core_DIR CACHE)
unset(Qt6Quick_DIR CACHE)
set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt@5")
set(Qt5_DIR "/opt/homebrew/opt/qt@5/lib/cmake/Qt5")
set(Qt5Core_DIR "/opt/homebrew/opt/qt@5/lib/cmake/Qt5Core")
set(Qt5Quick_DIR "/opt/homebrew/opt/qt@5/lib/cmake/Qt5Quick")
set(Qt5Multimedia_DIR "/opt/homebrew/opt/qt@5/lib/cmake/Qt5Multimedia")
include_directories("/opt/homebrew/opt/qt@5/include")
include_directories("/opt/homebrew/opt/qt@5/include/QtCore")
include_directories("/opt/homebrew/opt/qt@5/include/QtQuick")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig REQUIRED)

# Header search path for the FFmpeg libraries (FFMPEG_INCLUDE_DIRS is consumed below)
pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale libswresample)
find_library(AVFORMAT_LIBRARY avformat PATHS /opt/homebrew/lib)
find_library(AVCODEC_LIBRARY avcodec PATHS /opt/homebrew/lib)
find_library(AVUTIL_LIBRARY avutil PATHS /opt/homebrew/lib)
find_library(SWSCALE_LIBRARY swscale PATHS /opt/homebrew/lib)
find_library(SWRESAMPLE_LIBRARY swresample PATHS /opt/homebrew/lib)

set(FFMPEG_LIBRARIES
    ${AVFORMAT_LIBRARY}
    ${AVCODEC_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
include_directories("/opt/homebrew/include/SDL2/")
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
include_directories(${FFMPEG_INCLUDE_DIRS})

find_package(Qt5 COMPONENTS Quick Multimedia  REQUIRED)
message(STATUS "Qt5_VERSION: ${Qt5_VERSION}")
message(STATUS "Qt5_DIR: ${Qt5_DIR}")
message(STATUS "Qt5Core_INCLUDE_DIRS: ${Qt5Core_INCLUDE_DIRS}")
message(STATUS "Qt5Quick_INCLUDE_DIRS: ${Qt5Quick_INCLUDE_DIRS}")
include_directories(SYSTEM ${Qt5Core_INCLUDE_DIRS})


if(CMAKE_BUILD_TYPE MATCHES "Debug")
    message("build debug")
    add_definitions(-DDEBUG_MODE)
endif()


file(GLOB_RECURSE SRC_LIST
        3rd/rtl8812au-monitor-pcap/*.c
        3rd/rtl8812au-monitor-pcap/*.cpp
        src/*.cpp
        src/*.c
        src/*.h
)
list(FILTER SRC_LIST EXCLUDE REGEX ".*/3rd.*/src/main.cpp")

include_directories(src 3rd/rtl8812au-monitor-pcap/src 3rd/rtl8812au-monitor-pcap/hal)
add_executable(${PROJECT_NAME} ${SRC_LIST} qml.qrc)

target_link_libraries(${PROJECT_NAME}
        Qt5::Quick
        ${FFMPEG_LIBRARIES}
        PkgConfig::SDL2
        PkgConfig::LIBUSB
        PkgConfig::SODIUM)

option(FPV4WIN_BUILD_TESTS "Build decoder/renderer test programs (see tests/run_tests.sh)" OFF)
if (FPV4WIN_BUILD_TESTS)
    add_subdirectory(tests)
endif ()

# The app looks for config.ini / gs.key in its working directory; keep a copy of the sample key next to the binary.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/gs.key"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/gs.key")

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt5${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)
endif ()
