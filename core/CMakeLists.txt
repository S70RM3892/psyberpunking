# bench_core: シーン生成・描画・計測の共通C++コア。AndroidとLinuxの両ホストがこれをリンクする。
#
# 呼び出し側で FILAMENT_DIR（include/ と lib/<abi>/ を持つディレクトリ）と FILAMENT_ABI を決めておく。

set(BENCH_CORE_SOURCES
    src/config.cpp
    src/mesh.cpp
    src/procgen_city.cpp
    src/procgen_actors.cpp
    src/camera_path.cpp
    src/traffic.cpp
    src/measure.cpp
)

# 描画に依存しない部分（単体テストはこれだけをリンクする）
add_library(bench_model STATIC ${BENCH_CORE_SOURCES})
target_include_directories(bench_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${BENCH_ROOT}/third_party
    ${FILAMENT_DIR}/include)
target_compile_features(bench_model PUBLIC cxx_std_20)
target_compile_definitions(bench_model PUBLIC JSON_NOEXCEPTION=1)
target_compile_options(bench_model PRIVATE ${BENCH_CXX_FLAGS})

# 描画（Filament）
add_library(bench_core STATIC
    src/render/gpu.cpp
    src/render/textures.cpp
    src/render/presets.cpp
    src/render/fog.cpp
    src/render/particles.cpp
    src/render/world.cpp
    src/bench_app.cpp
    src/vulkan_info.cpp
)
target_include_directories(bench_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bench_core PUBLIC bench_model)
target_compile_options(bench_core PRIVATE ${BENCH_CXX_FLAGS})

set(_fl ${FILAMENT_DIR}/lib/${FILAMENT_ABI})
foreach(lib filament backend bluegl bluevk vkshaders filaflat filabridge geometry ibl ibl-lite filament-iblprefilter
            ktxreader basis_transcoder image utils abseil zstd smol-v perfetto uberzlib uberarchive shaders)
    if(EXISTS ${_fl}/lib${lib}.a)
        add_library(fil_${lib} STATIC IMPORTED)
        set_target_properties(fil_${lib} PROPERTIES IMPORTED_LOCATION ${_fl}/lib${lib}.a)
        list(APPEND BENCH_FILAMENT_LIBS fil_${lib})
    endif()
endforeach()
# 静的ライブラリ間の循環参照があるのでグループでリンクする
target_link_libraries(bench_core PUBLIC "-Wl,--start-group" ${BENCH_FILAMENT_LIBS} "-Wl,--end-group")
if(ANDROID)
    target_link_libraries(bench_core PUBLIC android log EGL GLESv3 dl)
else()
    target_link_libraries(bench_core PUBLIC dl pthread)
endif()
