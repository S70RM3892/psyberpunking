# ドライブ（別アプリ）のネイティブ部分。ベンチと同じ core/ と、drive/ をビルドする
cmake_minimum_required(VERSION 3.22)
project(citydrive_jni LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

get_filename_component(BENCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)
set(FILAMENT_DIR ${BENCH_ROOT}/third_party/filament/android)
set(FILAMENT_ABI ${ANDROID_ABI})
set(BENCH_CXX_FLAGS -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
set(BENCH_BUILD_HOST OFF)
set(BENCH_BUILD_TESTS OFF)

if(NOT EXISTS ${FILAMENT_DIR}/lib/${FILAMENT_ABI}/libfilament.a)
    message(FATAL_ERROR "Filament for Android not found. Run tools/fetch_filament.sh android first.")
endif()

add_subdirectory(${BENCH_ROOT}/core ${CMAKE_BINARY_DIR}/core)
add_subdirectory(${BENCH_ROOT}/drive ${CMAKE_BINARY_DIR}/drive)

add_library(citydrive SHARED drive_jni.cpp drive_host.cpp)
target_link_libraries(citydrive PRIVATE drive_core android log aaudio)
target_compile_options(citydrive PRIVATE ${BENCH_CXX_FLAGS})
# 16KBページ端末（Android 15+）対応
target_link_options(citydrive PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--gc-sections")
