#include "graphics_adapters.h"

#if defined(_WIN32)

#include <windows.h>

#include <cwchar>
#include <iterator>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace ssf2t {
namespace {

using CreateDeviceFunction = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

std::string Utf8Name(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string name(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, name.data(), size, nullptr, nullptr);
  return name;
}

// The player's choice for this program in the Windows graphics settings: 1 power saving, 2 high performance.
uint32_t UserGpuPreference() {
  wchar_t path[1024];
  const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  if (length == 0 || length >= std::size(path)) {
    return 0;
  }
  wchar_t value[256];
  DWORD value_size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", path, RRF_RT_REG_SZ,
                   nullptr, value, &value_size) != ERROR_SUCCESS) {
    return 0;
  }
  const wchar_t* preference = wcsstr(value, L"GpuPreference=");
  return preference != nullptr ? uint32_t(wcstoul(preference + 14, nullptr, 10)) : 0;
}

}  // namespace

std::vector<GraphicsAdapter> ListGraphicsAdapters() {
  std::vector<GraphicsAdapter> adapters;
  const HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto create_device =
      d3d12 ? reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (!create_device || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return adapters;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index, adapter.Reset()) {
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
        FAILED(create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
      continue;
    }
    adapters.push_back({int(index), Utf8Name(desc.Description)});
  }
  return adapters;
}

GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index) {
  GraphicsAdapterDetails details;
  const HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto create_device =
      d3d12 ? reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  Microsoft::WRL::ComPtr<IDXGIFactory6> preferred_factory;
  if (!create_device || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    if (d3d12) {
      FreeLibrary(d3d12);
    }
    return details;
  }

  const bool by_preference = configured_index == -1 && SUCCEEDED(factory.As(&preferred_factory));
  const DXGI_GPU_PREFERENCE preference =
      UserGpuPreference() == 1 ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

  UINT index = 0;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (;; ++index, adapter.Reset()) {
    const HRESULT result =
        by_preference ? preferred_factory->EnumAdapterByGpuPreference(index, preference, IID_PPV_ARGS(&adapter))
                      : factory->EnumAdapters1(index, &adapter);
    if (result != S_OK) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    const bool supported =
        SUCCEEDED(create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
    const bool chosen = configured_index >= 0 ? int(index) == configured_index && supported
                        : configured_index == -2 ? software && supported
                                                 : !software && supported;
    if (!chosen) {
      continue;
    }
    details.index = int(index);
    details.name = Utf8Name(desc.Description);
    details.vendor_id = desc.VendorId;
    details.device_id = desc.DeviceId;
    details.known = true;
    break;
  }
  if (d3d12) {
    FreeLibrary(d3d12);
  }
  return details;
}

}  // namespace ssf2t

#else

#include <dlfcn.h>

#include <algorithm>

#include <vulkan/vulkan_core.h>

namespace ssf2t {
namespace {

struct VulkanDeviceRecord {
  int index;
  GraphicsAdapterDetails details;
  int rank;
};

int DeviceTypeRank(VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return 1;
    default:
      return 0;
  }
}

// Index = position in vkEnumeratePhysicalDevices, which is what ReXGlue's vulkan_device counts. Software
// rasterizers are left out like WARP on Windows; the best hardware device comes first.
std::vector<VulkanDeviceRecord> EnumerateVulkanDevices(const char* library_name) {
  std::vector<VulkanDeviceRecord> devices;
  void* const library = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    return devices;
  }
  const auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
  const auto create = get_proc ? reinterpret_cast<PFN_vkCreateInstance>(get_proc(nullptr, "vkCreateInstance")) : nullptr;
  if (create == nullptr) {
    return devices;
  }
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.pApplicationInfo = &application;
  VkInstance instance = VK_NULL_HANDLE;
  if (create(&info, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
    return devices;
  }
  const auto enumerate =
      reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_proc(instance, "vkEnumeratePhysicalDevices"));
  const auto properties_of = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
      get_proc(instance, "vkGetPhysicalDeviceProperties"));
  const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(get_proc(instance, "vkDestroyInstance"));
  uint32_t count = 0;
  if (enumerate != nullptr && properties_of != nullptr && enumerate(instance, &count, nullptr) == VK_SUCCESS &&
      count > 0) {
    std::vector<VkPhysicalDevice> physical(count);
    if (enumerate(instance, &count, physical.data()) == VK_SUCCESS || count > 0) {
      for (uint32_t index = 0; index < count && index < physical.size(); ++index) {
        VkPhysicalDeviceProperties properties;
        properties_of(physical[index], &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
          continue;
        }
        GraphicsAdapterDetails details;
        details.index = int(index);
        details.name = properties.deviceName;
        details.vendor_id = properties.vendorID;
        details.device_id = properties.deviceID;
        details.known = true;
        devices.push_back({int(index), details, DeviceTypeRank(properties.deviceType)});
      }
    }
  }
  if (destroy != nullptr) {
    destroy(instance, nullptr);
  }
  std::stable_sort(devices.begin(), devices.end(),
                   [](const VulkanDeviceRecord& a, const VulkanDeviceRecord& b) { return a.rank > b.rank; });
  return devices;
}

constexpr char kVulkanLoader[] = "libvulkan.so.1";

}  // namespace

std::vector<GraphicsAdapter> ListVulkanAdapters(const char* library) {
  std::vector<GraphicsAdapter> adapters;
  for (const VulkanDeviceRecord& device : EnumerateVulkanDevices(library)) {
    adapters.push_back({device.index, device.details.name});
  }
  return adapters;
}

GraphicsAdapterDetails EffectiveVulkanAdapter(const char* library, int configured_index) {
  for (const VulkanDeviceRecord& device : EnumerateVulkanDevices(library)) {
    if (configured_index < 0 || device.index == configured_index) {
      return device.details;
    }
  }
  return {};
}

std::vector<GraphicsAdapter> ListGraphicsAdapters() {
  return ListVulkanAdapters(kVulkanLoader);
}

GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index) {
  return EffectiveVulkanAdapter(kVulkanLoader, configured_index);
}

}  // namespace ssf2t

#endif
