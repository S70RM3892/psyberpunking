# ドライブ（ベンチとは別アプリ）。街・描画は bench_core を共有し、車・当たり判定・カメラをここに持つ。

# 描画に依存しない部分（単体テストはこれだけをリンクする）
add_library(drive_model STATIC
    core/src/collision.cpp
    core/src/vehicle.cpp
    core/src/chase_camera.cpp
    core/src/autopilot.cpp
    core/src/audio_synth.cpp
)
target_include_directories(drive_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
target_link_libraries(drive_model PUBLIC bench_model)
target_compile_options(drive_model PRIVATE ${BENCH_CXX_FLAGS})

# ゲームループと描画（bench_core の World を使う）
add_library(drive_core STATIC core/src/drive_app.cpp)
target_include_directories(drive_core PRIVATE ${BENCH_ROOT}/core/src)
target_link_libraries(drive_core PUBLIC drive_model bench_core)
target_compile_options(drive_core PRIVATE ${BENCH_CXX_FLAGS})

if(BENCH_BUILD_HOST)
    add_subdirectory(linux)
endif()
if(BENCH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
