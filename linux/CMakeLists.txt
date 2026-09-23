find_package(SDL2 REQUIRED)

add_executable(benchdeck main.cpp)
target_link_libraries(benchdeck PRIVATE bench_core SDL2::SDL2)
target_compile_options(benchdeck PRIVATE ${BENCH_CXX_FLAGS})
target_compile_definitions(benchdeck PRIVATE
    BENCH_APP_VERSION="${PROJECT_VERSION}"
    BENCH_SOURCE_DIR="${BENCH_ROOT}")
add_dependencies(benchdeck bench_materials)

# 焼き済みカメラパスの生成（描画なし）
add_executable(bench_bake ${BENCH_ROOT}/tools/bake_camera_path.cpp)
target_link_libraries(bench_bake PRIVATE bench_model)

# 配布用の data/（マテリアル・シーン定義・カメラパス・変換済み素材）をバイナリの隣に揃える
add_custom_target(bench_scene_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/data/scenes
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BENCH_ROOT}/scenes/scene_v1.json ${BENCH_ROOT}/scenes/camera_path_v1.bin
            ${CMAKE_BINARY_DIR}/data/scenes/
    VERBATIM)
add_dependencies(benchdeck bench_scene_data)

install(TARGETS benchdeck DESTINATION .)
install(DIRECTORY ${CMAKE_BINARY_DIR}/data/ DESTINATION data)
install(FILES ${BENCH_ROOT}/scenes/scene_v1.json ${BENCH_ROOT}/scenes/camera_path_v1.bin DESTINATION data/scenes)
install(DIRECTORY ${BENCH_ROOT}/assets/ DESTINATION data OPTIONAL)
set_target_properties(benchdeck PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
