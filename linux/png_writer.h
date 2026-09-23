// 依存なしの最小PNG書き出し（RGBA8、無圧縮deflate）。画像差分とスクリーンショット用。
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace benchlinux {

inline uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

inline bool writePng(const std::string& path, const uint8_t* rgba, uint32_t w, uint32_t h, bool flipY) {
    // 生データ（各行の先頭にフィルタ種別 0）
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 4 + 1));
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t sy = flipY ? h - 1 - y : y;
        raw.push_back(0);
        const uint8_t* row = rgba + static_cast<size_t>(sy) * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            raw.push_back(row[x * 4]);
            raw.push_back(row[x * 4 + 1]);
            raw.push_back(row[x * 4 + 2]);
            raw.push_back(255);
        }
    }
    // zlib（無圧縮ブロック）
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xff));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xff));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t adler = (b << 16) | a;
    for (int i = 3; i >= 0; --i) z.push_back(static_cast<uint8_t>(adler >> (8 * i)));

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [&](uint32_t v) {
        uint8_t b4[4] = {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
                         static_cast<uint8_t>(v)};
        std::fwrite(b4, 1, 4, f);
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        std::fwrite(td.data(), 1, td.size(), f);
        be32(crc32(td.data(), td.size()));
    };
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {static_cast<uint8_t>(w >> 24), static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 8),
                                 static_cast<uint8_t>(w),       static_cast<uint8_t>(h >> 24), static_cast<uint8_t>(h >> 16),
                                 static_cast<uint8_t>(h >> 8),  static_cast<uint8_t>(h),       8, 6, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return std::fclose(f) == 0;
}

}  // namespace benchlinux
