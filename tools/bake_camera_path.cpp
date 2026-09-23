// scenes/scene_v1.json から固定カメラパスを生成し、30Hzのキーをバイナリに焼く。
//   bench_bake scenes/scene_v1.json scenes/camera_path_v1.bin
#include <cstdio>
#include <fstream>
#include <sstream>

#include "bench/camera_path.h"
#include "bench/config.h"
#include "bench/procgen.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <scene.json> <out.bin>\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string err;
    auto cfg = bench::parseSceneConfig(ss.str(), &err);
    if (!cfg) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    bench::CityData city = bench::generateCity(*cfg);
    bench::CameraPath path = bench::buildCameraPath(*cfg, city);
    std::vector<uint8_t> bytes = path.serialize();
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::printf("%s: %zu keys @ %d Hz, city hash %016llx\n", argv[2], path.keys.size(), path.rateHz,
                static_cast<unsigned long long>(city.hash()));
    return 0;
}
