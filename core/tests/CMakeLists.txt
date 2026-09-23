# gtest は libc++ で自前ビルドする（ディストリの gtest は libstdc++ 版で混ぜられない）
include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(bench_tests
    test_config.cpp
    test_procgen.cpp
    test_measure.cpp
    test_camera_path.cpp
)
target_link_libraries(bench_tests PRIVATE bench_model GTest::gtest GTest::gtest_main)
target_compile_definitions(bench_tests PRIVATE BENCH_SCENE_JSON="${BENCH_ROOT}/scenes/scene_v1.json"
                                               BENCH_CAMERA_BIN="${BENCH_ROOT}/scenes/camera_path_v1.bin")
include(GoogleTest)
gtest_discover_tests(bench_tests)
