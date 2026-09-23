#include "bench/vulkan_info.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <dlfcn.h>

namespace bench {

namespace {

// 必要な分だけの Vulkan の型（vulkan_core.h を持ち込まないため）
using VkFlags = uint32_t;
using VkResult = int32_t;
using VkInstance = struct VkInstance_T*;
using VkPhysicalDevice = struct VkPhysicalDevice_T*;

struct VkApplicationInfo {
    int32_t sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    int32_t sType;
    const void* pNext;
    VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    int32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint8_t limitsAndSparse[1024];  // 使わない（VkPhysicalDeviceLimits + SparseProperties より大きく確保）
};

using PFN_vkGetInstanceProcAddr = void* (*)(VkInstance, const char*);
using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
using PFN_vkDestroyInstance = void (*)(VkInstance, const void*);
using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t*, VkPhysicalDevice*);
using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, VkPhysicalDeviceProperties*);

std::string versionString(uint32_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", (v >> 22) & 0x7f, (v >> 12) & 0x3ff, v & 0xfff);
    return buf;
}

// ドライバ版の書式はベンダーごとに違う（NVIDIA 0x10DE と Qualcomm 0x5143 は独自）
std::string driverString(uint32_t vendor, uint32_t v) {
    char buf[48];
    if (vendor == 0x10DE) {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (v >> 22) & 0x3ff, (v >> 14) & 0xff, (v >> 6) & 0xff, v & 0x3f);
    } else if (vendor == 0x5143) {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u", (v >> 22) & 0x3ff, (v >> 12) & 0x3ff, v & 0xfff);
    } else {
        return versionString(v);
    }
    return buf;
}

}  // namespace

VulkanInfo queryVulkanInfo() {
    VulkanInfo info;
    void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return info;
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    if (!gipa) {
        dlclose(lib);
        return info;
    }
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{0 /*VK_STRUCTURE_TYPE_APPLICATION_INFO*/, nullptr, "benchdeck", 1, "benchdeck", 1, (1u << 22) | (1u << 12)};
    VkInstanceCreateInfo ci{1 /*VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO*/, nullptr, 0, &app, 0, nullptr, 0, nullptr};
    VkInstance instance = nullptr;
    if (create && create(&ci, nullptr, &instance) == 0 && instance) {
        auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
        auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
        uint32_t n = 0;
        if (enumerate && props && enumerate(instance, &n, nullptr) == 0 && n > 0) {
            std::vector<VkPhysicalDevice> devs(n);
            enumerate(instance, &n, devs.data());
            // 独立GPU（2）> 内蔵GPU（1）> その他 の順に選ぶ（Filament と同じ優先）
            int best = 0, bestRank = -1;
            for (uint32_t i = 0; i < n; ++i) {
                VkPhysicalDeviceProperties p{};
                props(devs[i], &p);
                int rank = p.deviceType == 2 ? 3 : p.deviceType == 1 ? 2 : p.deviceType == 3 ? 1 : 0;
                if (rank > bestRank) {
                    bestRank = rank;
                    best = static_cast<int>(i);
                }
            }
            VkPhysicalDeviceProperties p{};
            props(devs[best], &p);
            info.available = true;
            info.deviceName = std::string(p.deviceName, strnlen(p.deviceName, sizeof(p.deviceName)));
            info.apiVersionRaw = p.apiVersion;
            info.apiVersion = versionString(p.apiVersion);
            info.driverVersion = driverString(p.vendorID, p.driverVersion);
        }
        if (destroy) destroy(instance, nullptr);
    }
    dlclose(lib);
    return info;
}

}  // namespace bench
