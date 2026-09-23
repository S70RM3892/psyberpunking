# APK のネイティブ部分。Linux版と同じ core/ をそのままビルドする（Deckで基準を取ったコード = APKのコード）
cmake_minimum_required(VERSION 3.22)
project(benchdeck_jni LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

get_filename_component(BENCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)
set(FILAMENT_DIR ${BENCH_ROOT}/third_party/filament/android)
set(FILAMENT_ABI ${ANDROID_ABI})
set(BENCH_CXX_FLAGS -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

if(NOT EXISTS ${FILAMENT_DIR}/lib/${FILAMENT_ABI}/libfilament.a)
    message(FATAL_ERROR "Filament for Android not found. Run tools/fetch_filament.sh android first.")
endif()

add_subdirectory(${BENCH_ROOT}/core ${CMAKE_BINARY_DIR}/core)

add_library(benchdeck SHARED jni_bridge.cpp android_host.cpp)
target_link_libraries(benchdeck PRIVATE bench_core android log)
target_compile_options(benchdeck PRIVATE ${BENCH_CXX_FLAGS})
# 16KBページ端末（Android 15+）対応：ELF のセグメントを 16KB 境界に揃える
target_link_options(benchdeck PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--gc-sections")
