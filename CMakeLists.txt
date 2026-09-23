# BenchDeck — Linux版（Steam Deckでの基準取り）と単体テスト、マテリアルのビルド。
# Android版は android/ のGradleプロジェクトが同じ core/ をCMakeでビルドする。
cmake_minimum_required(VERSION 3.22)
project(benchdeck VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# Filament の Linux 版は clang + libc++ でビルドされている。ABIを合わせるため、こちらも libc++ を使う
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Build with clang (CC=clang CXX=clang++): Filament's Linux release links against libc++")
endif()
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
add_link_options(-stdlib=libc++)

set(BENCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(FILAMENT_DIR ${BENCH_ROOT}/third_party/filament/linux CACHE PATH "Filament (linux) release directory")
set(FILAMENT_ABI x86_64)
# Filamentに合わせて例外・RTTIを切る
set(BENCH_CXX_FLAGS -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

option(BENCH_BUILD_HOST "Build the Linux SDL2 host" ON)
option(BENCH_BUILD_TESTS "Build unit tests" ON)

if(NOT EXISTS ${FILAMENT_DIR}/include/filament/Engine.h)
    message(FATAL_ERROR "Filament not found in ${FILAMENT_DIR}. Run tools/fetch_filament.sh linux first.")
endif()

# ---- マテリアル（matc でビルド時にコンパイル。Vulkan向けSPIR-Vを含める） ----
set(MATC ${FILAMENT_DIR}/bin/matc)
file(GLOB BENCH_MATERIALS ${BENCH_ROOT}/materials/*.mat)
set(BENCH_MATERIAL_OUT ${CMAKE_BINARY_DIR}/data/materials)
set(BENCH_FILAMATS)
foreach(mat ${BENCH_MATERIALS})
    get_filename_component(name ${mat} NAME_WE)
    set(out ${BENCH_MATERIAL_OUT}/${name}.filamat)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCH_MATERIAL_OUT}
        COMMAND ${MATC} --api vulkan --platform all -o ${out} ${mat}
        DEPENDS ${mat} ${BENCH_ROOT}/materials/common.glsl
        COMMENT "matc ${name}.mat"
        VERBATIM)
    list(APPEND BENCH_FILAMATS ${out})
endforeach()
add_custom_target(bench_materials ALL DEPENDS ${BENCH_FILAMATS})

add_subdirectory(core)

if(BENCH_BUILD_HOST)
    add_subdirectory(linux)
endif()

if(BENCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()
