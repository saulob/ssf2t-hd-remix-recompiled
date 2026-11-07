// The program trg-updater installs and restarts in ssf2t_update_helper_tests: it records how it was started in
// <its own path>.marker and exits. GUI subsystem because the helper restarts it without CREATE_NO_WINDOW.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

void Write(HANDLE file, const wchar_t* text) {
  const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) {
    return;
  }
  char buffer[4096];
  if (length > int(sizeof(buffer))) {
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, length, nullptr, nullptr);
  DWORD written = 0;
  WriteFile(file, buffer, DWORD(length - 1), &written, nullptr);
}

void WriteVariable(HANDLE file, const wchar_t* name) {
  Write(file, L"env_");
  Write(file, name);
  Write(file, GetEnvironmentVariableW(name, nullptr, 0) > 0 ? L"=present\n" : L"=absent\n");
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t path[MAX_PATH + 8];
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return 2;
  }
  lstrcpyW(path + length, L".marker");
  const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return 3;
  }
  Write(file, L"command_line=");
  Write(file, GetCommandLineW());
  Write(file, L"\n");
  WriteVariable(file, L"SSF2T_UPDATE_TEST");
  WriteVariable(file, L"SSF2T_UPDATE_TEST_CURRENT_VERSION");
  WriteVariable(file, L"SSF2T_UPDATE_TEST_RELEASE_JSON");
  WriteVariable(file, L"SSF2T_UPDATE_TEST_ASSET_SHA256");
  Write(file, L"done=1\n");
  CloseHandle(file);
  return 0;
}
