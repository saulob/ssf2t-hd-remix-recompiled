#include "graphics_adapters.h"

#if defined(_WIN32)

#include <windows.h>

#include <d3d12.h>
#include <dxgi.h>
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

}  // namespace ssf2t

#else

namespace ssf2t {

std::vector<GraphicsAdapter> ListGraphicsAdapters() {
  return {};
}

}  // namespace ssf2t

#endif
