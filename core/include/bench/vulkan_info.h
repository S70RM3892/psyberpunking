// 端末のGPU名・ドライバ版・Vulkan版を調べる（libvulkan を dlopen するのでリンク依存は増えない）
#pragma once

#include <cstdint>
#include <string>

namespace bench {

struct VulkanInfo {
    bool available = false;
    std::string deviceName;
    std::string driverVersion;
    std::string apiVersion;    // "1.3.275"
    uint32_t apiVersionRaw = 0;
};

VulkanInfo queryVulkanInfo();

}  // namespace bench
