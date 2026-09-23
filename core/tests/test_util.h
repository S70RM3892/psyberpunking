#pragma once

#include <fstream>
#include <sstream>
#include <string>

#include "bench/config.h"

inline std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline bench::SceneConfig loadScene() {
    std::string err;
    auto c = bench::parseSceneConfig(readFile(BENCH_SCENE_JSON), &err);
    if (!c) {
        fprintf(stderr, "%s\n", err.c_str());
        abort();
    }
    return *c;
}
