add_executable(drive_tests test_vehicle.cpp test_collision.cpp test_audio.cpp)
target_link_libraries(drive_tests PRIVATE drive_model GTest::gtest GTest::gtest_main)
target_include_directories(drive_tests PRIVATE ${BENCH_ROOT}/core/tests)
target_compile_definitions(drive_tests PRIVATE BENCH_SCENE_JSON="${BENCH_ROOT}/scenes/scene_v1.json")
include(GoogleTest)  # 変数（発見用スクリプトのパス）はディレクトリごとなので、ここでも読む
gtest_discover_tests(drive_tests)
