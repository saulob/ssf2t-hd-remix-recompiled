// A stand-in for libvulkan.so.1 that the graphics card tests load through the production enumeration code. The devices
// come from SSF2T_FAKE_VULKAN, "type:name" entries separated by ';' with type discrete, integrated, virtual or cpu.
// SSF2T_FAKE_VULKAN_FAIL=create makes vkCreateInstance fail.

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct FakeDevice {
  VkPhysicalDeviceType type;
  std::string name;
};

std::vector<FakeDevice> Devices() {
  std::vector<FakeDevice> devices;
  const char* spec = std::getenv("SSF2T_FAKE_VULKAN");
  if (spec == nullptr) {
    return devices;
  }
  const std::string text = spec;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(';', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string entry = text.substr(start, end - start);
    const size_t colon = entry.find(':');
    if (colon != std::string::npos) {
      const std::string kind = entry.substr(0, colon);
      VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
      if (kind == "discrete") type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
      if (kind == "integrated") type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
      if (kind == "virtual") type = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
      if (kind == "cpu") type = VK_PHYSICAL_DEVICE_TYPE_CPU;
      devices.push_back({type, entry.substr(colon + 1)});
    }
    start = end + 1;
  }
  return devices;
}

VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* instance) {
  const char* fail = std::getenv("SSF2T_FAKE_VULKAN_FAIL");
  if (fail != nullptr && std::strcmp(fail, "create") == 0) {
    return VK_ERROR_INCOMPATIBLE_DRIVER;
  }
  *instance = reinterpret_cast<VkInstance>(std::uintptr_t(1));
  return VK_SUCCESS;
}

void VKAPI_CALL DestroyInstance(VkInstance, const VkAllocationCallbacks*) {}

VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance, uint32_t* count, VkPhysicalDevice* out) {
  const std::vector<FakeDevice> devices = Devices();
  if (out == nullptr) {
    *count = uint32_t(devices.size());
    return VK_SUCCESS;
  }
  const uint32_t written = *count < devices.size() ? *count : uint32_t(devices.size());
  for (uint32_t i = 0; i < written; ++i) {
    out[i] = reinterpret_cast<VkPhysicalDevice>(std::uintptr_t(i + 1));
  }
  *count = written;
  return written < devices.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice device, VkPhysicalDeviceProperties* properties) {
  const std::vector<FakeDevice> devices = Devices();
  const size_t index = reinterpret_cast<std::uintptr_t>(device) - 1;
  *properties = {};
  if (index >= devices.size()) {
    return;
  }
  properties->deviceType = devices[index].type;
  properties->vendorID = 0x10DE + uint32_t(index);
  properties->deviceID = 0x2000 + uint32_t(index);
  std::strncpy(properties->deviceName, devices[index].name.c_str(), VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance, const char* name) {
  const std::string function = name;
  if (function == "vkCreateInstance") return reinterpret_cast<PFN_vkVoidFunction>(CreateInstance);
  if (function == "vkDestroyInstance") return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
  if (function == "vkEnumeratePhysicalDevices") return reinterpret_cast<PFN_vkVoidFunction>(EnumeratePhysicalDevices);
  if (function == "vkGetPhysicalDeviceProperties") return reinterpret_cast<PFN_vkVoidFunction>(GetPhysicalDeviceProperties);
  return nullptr;
}
