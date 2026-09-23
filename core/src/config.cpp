#include "bench/config.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace bench {

// ordered_json: キー順を保ったまま書き戻す（baseline の追記でファイルが並べ替わらないように）
using json = nlohmann::ordered_json;

namespace {

// 例外なしでJSONを読むための小さなリーダ。最初のエラーだけを残し、以降は既定値を返す。
class Reader {
public:
    std::string error;
    bool ok() const { return error.empty(); }

    void fail(const std::string& path, const char* why) {
        if (error.empty()) error = "scene json: '" + path + "' " + why;
    }

    const json* child(const json& j, const std::string& path, const char* key, bool required) {
        if (!j.is_object()) {
            fail(path, "is not an object");
            return nullptr;
        }
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) {
            if (required) fail(path + "." + key, "is missing");
            return nullptr;
        }
        return &*it;
    }

    double num(const json& j, const std::string& path, const char* key, double fallback, bool required) {
        const json* v = child(j, path, key, required);
        if (!v) return fallback;
        if (!v->is_number()) {
            fail(path + "." + key, "must be a number");
            return fallback;
        }
        return v->get<double>();
    }

    int integer(const json& j, const std::string& path, const char* key, int fallback, bool required) {
        const json* v = child(j, path, key, required);
        if (!v) return fallback;
        if (!v->is_number_integer()) {
            fail(path + "." + key, "must be an integer");
            return fallback;
        }
        return v->get<int>();
    }

    bool boolean(const json& j, const std::string& path, const char* key, bool fallback, bool required) {
        const json* v = child(j, path, key, required);
        if (!v) return fallback;
        if (!v->is_boolean()) {
            fail(path + "." + key, "must be true/false");
            return fallback;
        }
        return v->get<bool>();
    }

    std::string str(const json& j, const std::string& path, const char* key, std::string fallback, bool required) {
        const json* v = child(j, path, key, required);
        if (!v) return fallback;
        if (!v->is_string()) {
            fail(path + "." + key, "must be a string");
            return fallback;
        }
        return v->get<std::string>();
    }

    template <typename T, size_t N>
    std::array<T, N> arr(const json& j, const std::string& path, const char* key, std::array<T, N> fallback,
                         bool required) {
        const json* v = child(j, path, key, required);
        if (!v) return fallback;
        if (!v->is_array() || v->size() != N) {
            fail(path + "." + key, ("must be an array of " + std::to_string(N) + " numbers").c_str());
            return fallback;
        }
        std::array<T, N> r{};
        for (size_t i = 0; i < N; ++i) {
            if (!(*v)[i].is_number()) {
                fail(path + "." + key, "must contain numbers only");
                return fallback;
            }
            r[i] = static_cast<T>((*v)[i].get<double>());
        }
        return r;
    }

    std::vector<std::string> strings(const json& j, const std::string& path, const char* key) {
        std::vector<std::string> r;
        const json* v = child(j, path, key, false);
        if (!v) return r;
        if (!v->is_array()) {
            fail(path + "." + key, "must be an array of strings");
            return r;
        }
        for (const auto& e : *v) {
            if (!e.is_string()) {
                fail(path + "." + key, "must contain strings only");
                return {};
            }
            r.push_back(e.get<std::string>());
        }
        return r;
    }
};

Quality parseQuality(Reader& rd, const std::string& path, const std::string& s) {
    if (s == "low") return Quality::Low;
    if (s == "medium") return Quality::Medium;
    if (s == "high") return Quality::High;
    rd.fail(path, "must be low/medium/high");
    return Quality::Low;
}

Preset parsePreset(Reader& rd, const std::string& name, const json& p) {
    const std::string base = "presets." + name;
    Preset r;
    r.name = name;
    r.renderScale = static_cast<float>(rd.num(p, base, "render_scale", 0.67, true));
    r.upscaler = rd.str(p, base, "upscaler", "fsr", false);

    static const json kEmpty = json::object();
    const json* sh = rd.child(p, base, "shadow", true);
    const json& shadow = sh ? *sh : kEmpty;
    r.shadow.cascades = rd.integer(shadow, base + ".shadow", "cascades", 3, true);
    r.shadow.map = rd.integer(shadow, base + ".shadow", "map", 1024, true);
    r.shadow.localLights = rd.integer(shadow, base + ".shadow", "local_lights", 8, true);
    r.shadow.localMap = rd.integer(shadow, base + ".shadow", "local_map", 512, true);
    r.shadow.contact = rd.boolean(shadow, base + ".shadow", "contact", true, true);

    const json* aoj = rd.child(p, base, "ssao", true);
    const json& ao = aoj ? *aoj : kEmpty;
    r.ssao.enabled = rd.boolean(ao, base + ".ssao", "enabled", true, true);
    r.ssao.halfRes = rd.boolean(ao, base + ".ssao", "half_res", true, false);
    r.ssao.quality = parseQuality(rd, base + ".ssao.quality", rd.str(ao, base + ".ssao", "quality", "low", false));

    const json* ssrj = rd.child(p, base, "ssr", true);
    const json& ssr = ssrj ? *ssrj : kEmpty;
    r.ssr.enabled = rd.boolean(ssr, base + ".ssr", "enabled", true, true);
    r.ssr.halfRes = rd.boolean(ssr, base + ".ssr", "half_res", true, false);
    r.ssr.steps = rd.integer(ssr, base + ".ssr", "steps", 24, false);

    const json* fogj = rd.child(p, base, "fog", true);
    const json& fog = fogj ? *fogj : kEmpty;
    r.fog.enabled = rd.boolean(fog, base + ".fog", "enabled", true, true);
    r.fog.grid = rd.arr<int, 3>(fog, base + ".fog", "grid", {0, 0, 0}, false);

    r.crowdScale = static_cast<float>(rd.num(p, base, "crowd_scale", 1.0, true));
    r.textureMax = rd.integer(p, base, "texture_max", 1024, true);
    r.taa = rd.boolean(p, base, "taa", true, false);
    r.bloom = rd.boolean(p, base, "bloom", true, false);
    r.dof = rd.boolean(p, base, "dof", false, false);
    r.motionBlur = rd.boolean(p, base, "motion_blur", false, false);

    if (r.renderScale <= 0.f || r.renderScale > 1.f) rd.fail(base + ".render_scale", "must be in (0, 1]");
    if (r.shadow.cascades < 1 || r.shadow.cascades > 4) rd.fail(base + ".shadow.cascades", "must be 1..4");
    if (r.fog.enabled && (r.fog.grid[0] < 2 || r.fog.grid[1] < 2 || r.fog.grid[2] < 1)) {
        rd.fail(base + ".fog.grid", "must be at least [2, 2, 1] when fog is enabled");
    }
    if (r.ssr.enabled && r.ssr.steps < 1) rd.fail(base + ".ssr.steps", "must be >= 1 when ssr is enabled");
    return r;
}

}  // namespace

const Preset* SceneConfig::findPreset(std::string_view name) const {
    for (const auto& p : presets) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

int SceneConfig::crowdCountFor(const Preset& p) const {
    return static_cast<int>(std::lround(crowd.count * p.crowdScale));
}

int SceneConfig::sectionAt(double t) const {
    for (size_t i = 0; i < cameraPath.sections.size(); ++i) {
        if (t >= cameraPath.sections[i].t0 && t < cameraPath.sections[i].t1) return static_cast<int>(i);
    }
    return cameraPath.sections.empty() ? 0 : static_cast<int>(cameraPath.sections.size()) - 1;
}

std::optional<SceneConfig> parseSceneConfig(std::string_view text, std::string* error) {
    json j = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = "scene json: parse error (not a JSON object)";
        return std::nullopt;
    }

    Reader rd;
    static const json kEmpty = json::object();
    auto section = [&](const char* key, bool required) -> const json& {
        const json* v = rd.child(j, "root", key, required);
        return v ? *v : kEmpty;
    };

    SceneConfig c;
    c.version = rd.str(j, "root", "version", "", true);
    c.seed = static_cast<uint32_t>(rd.integer(j, "root", "seed", 2077, true));
    const json& target = section("target", true);
    c.targetDevice = rd.str(target, "target", "device", "steam_deck", true);
    c.targetAvgFps = rd.num(target, "target", "avg_fps", 40.0, true);
    if (const json* b = rd.child(j, "root", "baseline", false)) {
        Baseline bl;
        bl.avgFps = rd.num(*b, "baseline", "avg_fps", 0.0, true);
        bl.low1Fps = rd.num(*b, "baseline", "low1_fps", 0.0, true);
        if (bl.avgFps <= 0.0 || bl.low1Fps <= 0.0) rd.fail("baseline", "values must be positive");
        c.baseline = bl;
    }

    const json& render = section("render", true);
    c.width = rd.integer(render, "render", "width", 1280, true);
    c.height = rd.integer(render, "render", "height", 800, true);
    c.backend = rd.str(render, "render", "backend", "vulkan", true);

    const json& city = section("city", true);
    c.city.blocks = rd.arr<int, 2>(city, "city", "blocks", c.city.blocks, true);
    c.city.blockSize = rd.arr<float, 2>(city, "city", "block_size_m", c.city.blockSize, false);
    c.city.streetWidth = static_cast<float>(rd.num(city, "city", "street_width_m", c.city.streetWidth, false));
    c.city.alleyWidth = static_cast<float>(rd.num(city, "city", "alley_width_m", c.city.alleyWidth, false));
    c.city.buildings = rd.integer(city, "city", "buildings", c.city.buildings, true);
    c.city.lodDistances = rd.arr<float, 3>(city, "city", "lod_distances_m", c.city.lodDistances, true);
    c.city.heightRange = rd.arr<float, 2>(city, "city", "height_range_m", c.city.heightRange, false);
    if (const json* ov = rd.child(city, "city", "overpass", false)) {
        c.city.overpassHeight = static_cast<float>(rd.num(*ov, "city.overpass", "height_m", c.city.overpassHeight, false));
        c.city.overpassWidth = static_cast<float>(rd.num(*ov, "city.overpass", "width_m", c.city.overpassWidth, false));
    }

    const json& lights = section("lights", true);
    c.lights.neonPoint = rd.integer(lights, "lights", "neon_point", c.lights.neonPoint, true);
    c.lights.neonSpot = rd.integer(lights, "lights", "neon_spot", c.lights.neonSpot, true);
    c.lights.street = rd.integer(lights, "lights", "street", c.lights.street, true);
    c.lights.neonIntensity = static_cast<float>(rd.num(lights, "lights", "neon_intensity_lm", c.lights.neonIntensity, false));
    c.lights.streetIntensity = static_cast<float>(rd.num(lights, "lights", "street_intensity_lm", c.lights.streetIntensity, false));
    c.lights.falloff = static_cast<float>(rd.num(lights, "lights", "falloff_m", c.lights.falloff, false));
    c.lights.moonLux = static_cast<float>(rd.num(lights, "lights", "moon_lux", c.lights.moonLux, false));

    const json& crowd = section("crowd", true);
    c.crowd.model = rd.str(crowd, "crowd", "model", "", false);
    c.crowd.count = rd.integer(crowd, "crowd", "count", c.crowd.count, true);
    c.crowd.variants = rd.integer(crowd, "crowd", "variants", c.crowd.variants, false);
    c.crowd.walkSpeed = rd.arr<float, 2>(crowd, "crowd", "walk_speed_mps", c.crowd.walkSpeed, false);

    const json& veh = section("vehicles", true);
    c.vehicles.models = rd.strings(veh, "vehicles", "models");
    c.vehicles.count = rd.integer(veh, "vehicles", "count", c.vehicles.count, true);
    c.vehicles.speed = rd.arr<float, 2>(veh, "vehicles", "speed_mps", c.vehicles.speed, false);

    const json& part = section("particles", true);
    c.particles.rain = rd.integer(part, "particles", "rain", c.particles.rain, true);
    c.particles.steam = rd.integer(part, "particles", "steam", c.particles.steam, true);
    c.particles.rainBox = rd.arr<float, 3>(part, "particles", "rain_box_m", c.particles.rainBox, false);

    const json& fog = section("fog", false);
    c.fog.density = static_cast<float>(rd.num(fog, "fog", "density", c.fog.density, false));
    c.fog.heightFalloff = static_cast<float>(rd.num(fog, "fog", "height_falloff", c.fog.heightFalloff, false));
    c.fog.range = static_cast<float>(rd.num(fog, "fog", "range_m", c.fog.range, false));
    c.fog.anisotropyHint = static_cast<float>(rd.num(fog, "fog", "anisotropy_hint", c.fog.anisotropyHint, false));

    const json& cam = section("camera_path", true);
    c.cameraPath.duration = rd.num(cam, "camera_path", "duration_s", 60.0, true);
    c.cameraPath.rateHz = rd.integer(cam, "camera_path", "rate_hz", 30, false);
    c.cameraPath.keys = rd.str(cam, "camera_path", "keys", "", true);
    if (const json* secs = rd.child(cam, "camera_path", "sections", true)) {
        if (!secs->is_array() || secs->empty()) {
            rd.fail("camera_path.sections", "must be a non-empty array");
        } else {
            for (const auto& s : *secs) {
                auto t = rd.arr<double, 2>(s, "camera_path.sections", "t", {0, 0}, true);
                c.cameraPath.sections.push_back({rd.str(s, "camera_path.sections", "name", "", true), t[0], t[1]});
            }
        }
    }

    // 並び順を固定する（Low → Deck → Medium → High → その他）。JSONのオブジェクト順には依存しない
    static const char* kOrder[] = {"low", "deck", "medium", "high"};
    if (const json* presets = rd.child(j, "root", "presets", true)) {
        for (const char* name : kOrder) {
            if (presets->contains(name)) c.presets.push_back(parsePreset(rd, name, (*presets)[name]));
        }
        for (auto it = presets->begin(); it != presets->end(); ++it) {
            if (!c.findPreset(it.key())) c.presets.push_back(parsePreset(rd, it.key(), it.value()));
        }
    }
    if (rd.ok() && !c.findPreset("deck")) rd.fail("presets.deck", "is required (scores are always taken on it)");

    for (const auto& p : c.presets) {
        if (p.shadow.localLights > c.lights.neonSpot + c.lights.street) {
            rd.fail("presets." + p.name + ".shadow.local_lights", "exceeds the number of spot lights");
        }
    }
    if (c.width <= 0 || c.height <= 0) rd.fail("render", "width/height must be positive");
    if (c.cameraPath.rateHz <= 0) rd.fail("camera_path.rate_hz", "must be positive");

    if (!rd.ok()) {
        if (error) *error = rd.error;
        return std::nullopt;
    }
    return c;
}

std::string writeBaseline(std::string_view text, const Baseline& b) {
    json j = json::parse(text.begin(), text.end(), nullptr, false);
    if (j.is_discarded()) return {};
    j["baseline"] = {{"avg_fps", std::round(b.avgFps * 100.0) / 100.0},
                     {"low1_fps", std::round(b.low1Fps * 100.0) / 100.0}};
    return j.dump(2) + "\n";
}

}  // namespace bench
