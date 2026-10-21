// Real trg-updater flow from a temporary helper directory: parent wait, install, restart quoting and child
// environment cleanup.

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

class TempDir {
 public:
  TempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("SSF2T Update Test " + std::to_string(now));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

class ScopedEnvironment {
 public:
  ScopedEnvironment() {
    for (const wchar_t* name : kNames) {
      old_.push_back(Read(name));
      SetEnvironmentVariableW(name, L"test-value");
    }
  }
  ~ScopedEnvironment() {
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
      SetEnvironmentVariableW(kNames[i], old_[i] ? old_[i]->c_str() : nullptr);
    }
  }

 private:
  static constexpr const wchar_t* kNames[] = {
      L"SSF2T_UPDATE_TEST", L"SSF2T_UPDATE_TEST_CURRENT_VERSION", L"SSF2T_UPDATE_TEST_RELEASE_JSON",
      L"SSF2T_UPDATE_TEST_ASSET_SHA256"};

  static std::optional<std::wstring> Read(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
      return std::nullopt;
    }
    std::wstring value(size_t(length), L'\0');
    GetEnvironmentVariableW(name, value.data(), length);
    value.resize(size_t(length - 1));
    return value;
  }

  std::vector<std::optional<std::wstring>> old_;
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), std::streamsize(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::wstring Quote(const std::wstring& argument) {
  if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return argument;
  }
  std::wstring quoted = L"\"";
  size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted += L'"';
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      backslashes = 0;
      quoted += c;
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted += L'"';
  return quoted;
}

bool RunHelper(const std::filesystem::path& helper, const std::filesystem::path& job,
               const std::filesystem::path& working_directory, DWORD& exit_code, std::string& error) {
  std::wstring command = Quote(helper.wstring()) + L" --job " + Quote(job.wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      working_directory.c_str(), &startup, &process)) {
    error = "CreateProcessW failed (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  WaitForSingleObject(process.hProcess, 30000);
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

bool WaitForMarker(const std::filesystem::path& path) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::exists(path)) {
      const std::string text = ReadFile(path);
      if (text.find("done=1") != std::string::npos) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

std::vector<std::string> ImportedLibraries(const std::filesystem::path& executable) {
  std::vector<std::string> names;
  const std::string image = ReadFile(executable);
  const auto* base = reinterpret_cast<const unsigned char*>(image.data());
  if (image.size() < sizeof(IMAGE_DOS_HEADER)) {
    return names;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.size()) {
    return names;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
  auto to_offset = [&](DWORD rva) -> size_t {
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
      const IMAGE_SECTION_HEADER& section = sections[i];
      if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.Misc.VirtualSize) {
        return size_t(section.PointerToRawData + (rva - section.VirtualAddress));
      }
    }
    return image.size();
  };
  for (size_t at = to_offset(imports.VirtualAddress); imports.Size && at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= image.size();
       at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + at);
    if (descriptor->Name == 0) {
      break;
    }
    const size_t name = to_offset(descriptor->Name);
    if (name < image.size()) {
      names.emplace_back(image.c_str() + name);
    }
  }
  return names;
}

bool IsSystemLibrary(std::string name) {
  for (char& c : name) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  static const char* kSystem[] = {"kernel32.dll", "shell32.dll", "user32.dll", "advapi32.dll", "ntdll.dll",
                                  "shlwapi.dll", "ole32.dll", "bcrypt.dll"};
  if (name.rfind("api-ms-win-", 0) == 0) {
    return true;
  }
  for (const char* system : kSystem) {
    if (name == system) {
      return true;
    }
  }
  return false;
}

bool SameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
  return ReadFile(left) == ReadFile(right);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  ScopedEnvironment environment;
  TempDir temp;
  const std::filesystem::path helper_dir = temp.path / "helper dir";
  const std::filesystem::path helper = helper_dir / "trg-updater.exe";
  const std::filesystem::path standalone = temp.path / "standalone dir";
  const std::filesystem::path install = temp.path / "install dir";
  const std::filesystem::path staging = temp.path / "staging dir";
  const std::filesystem::path backup = temp.path / "backup dir";
  const std::filesystem::path workspace = temp.path / "update workspace";
  const std::filesystem::path manifest = workspace / "manifest.txt";
  const std::filesystem::path job = workspace / "job.txt";
  const std::filesystem::path log = workspace / "trg-updater.log";
  const std::filesystem::path marker_output = temp.path / "marker output.txt";
  const std::filesystem::path parent_output = temp.path / "parent output.txt";
  const std::filesystem::path marker = SSF2T_RESTART_MARKER_EXE;

  std::error_code ec;
  std::filesystem::create_directories(helper_dir, ec);
  std::filesystem::copy_file(SSF2T_UPDATER_EXE, helper, std::filesystem::copy_options::overwrite_existing, ec);
  CHECK(!ec);
  for (const auto& entry : std::filesystem::directory_iterator(helper_dir, ec)) {
    CHECK(entry.is_regular_file());
    CHECK(entry.path().extension() == ".exe");
  }

  const std::vector<std::string> imports = ImportedLibraries(helper);
  CHECK(!imports.empty());
  for (const std::string& library : imports) {
    if (!IsSystemLibrary(library)) {
      std::printf("  helper imports %s\n", library.c_str());
    }
    CHECK(IsSystemLibrary(library));
  }

  std::filesystem::create_directories(standalone, ec);
  DWORD standalone_exit = 1;
  std::string standalone_error;
  CHECK(RunHelper(helper, standalone / "missing job.txt", standalone, standalone_exit, standalone_error));
  CHECK(standalone_exit == 2);
  const std::string standalone_log = ReadFile(standalone / "trg-updater.log");
  CHECK(standalone_log.find("trg-updater starting") != std::string::npos);
  CHECK(standalone_log.find("job read failed") != std::string::npos);
  CHECK(standalone_log.find("exit code: 2") != std::string::npos);

  std::wstring parent_command = Quote(marker.wstring()) + L" --hold 750 " + Quote(parent_output.wstring());
  STARTUPINFOW parent_startup{};
  parent_startup.cb = sizeof(parent_startup);
  PROCESS_INFORMATION parent{};
  CHECK(CreateProcessW(marker.c_str(), parent_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       temp.path.c_str(), &parent_startup, &parent));
  CHECK(parent.dwProcessId != 0);

  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(install / "app.exe", "DEVELOPMENT_BUILD");
  WriteFile(install / "user_data" / "settings.toml", "player settings");
  WriteFile(install / "game" / "default.xex", "game files");
  WriteFile(install / "game" / "extracted" / "flog_x360.fpg", "game assets");
  WriteFile(install / "user_data" / "584108A4" / "savegames.dat", "save data");
  WriteFile(install / "logs" / "session.log", "log");
  WriteFile(install / "notes.txt", "keep me");
  std::filesystem::copy_file(marker, staging / "app.exe", std::filesystem::copy_options::overwrite_existing, ec);
  CHECK(!ec);
  WriteFile(manifest, "app.exe\n");

  std::string job_text = "format=1\n";
  job_text += "parent_pid=" + std::to_string(parent.dwProcessId) + "\n";
  job_text += "install_dir=" + install.string() + "\n";
  job_text += "staging_dir=" + staging.string() + "\n";
  job_text += "backup_dir=" + backup.string() + "\n";
  job_text += "manifest=" + manifest.string() + "\n";
  job_text += "restart=" + (install / "app.exe").string() + "\n";
  job_text += "log=" + log.string() + "\n";
  job_text += "arg=" + marker_output.string() + "\n";
  job_text += "arg=--flag=with space\n";
  job_text += "arg=--ssf2t_launcher=true\n";
  WriteFile(job, job_text);

  DWORD exit_code = 1;
  std::string error;
  CHECK(RunHelper(helper, job, workspace, exit_code, error));
  if (!error.empty()) {
    std::printf("  helper: %s\n", error.c_str());
  }
  WaitForSingleObject(parent.hProcess, 5000);
  CloseHandle(parent.hThread);
  CloseHandle(parent.hProcess);
  CHECK(exit_code == 0);
  CHECK(WaitForMarker(marker_output));
  CHECK(SameFile(install / "app.exe", marker));

  const std::string marker_text = ReadFile(marker_output);
  CHECK(marker_text.find("arg1=" + marker_output.string()) != std::string::npos);
  CHECK(marker_text.find("arg2=--flag=with space") != std::string::npos);
  CHECK(marker_text.find("arg3=--ssf2t_launcher=true") != std::string::npos);
  CHECK(marker_text.find("env_SSF2T_UPDATE_TEST=absent") != std::string::npos);
  CHECK(marker_text.find("env_SSF2T_UPDATE_TEST_CURRENT_VERSION=absent") != std::string::npos);
  CHECK(marker_text.find("env_SSF2T_UPDATE_TEST_RELEASE_JSON=absent") != std::string::npos);
  CHECK(marker_text.find("env_SSF2T_UPDATE_TEST_ASSET_SHA256=absent") != std::string::npos);

  CHECK(ReadFile(install / "user_data" / "settings.toml") == "player settings");
  CHECK(ReadFile(install / "game" / "default.xex") == "game files");
  CHECK(ReadFile(install / "game" / "extracted" / "flog_x360.fpg") == "game assets");
  CHECK(ReadFile(install / "user_data" / "584108A4" / "savegames.dat") == "save data");
  CHECK(ReadFile(install / "logs" / "session.log") == "log");
  CHECK(ReadFile(install / "notes.txt") == "keep me");

  const std::string helper_log = ReadFile(log);
  CHECK(helper_log.find("install root: " + install.string()) != std::string::npos);
  CHECK(helper_log.find("staging root: " + staging.string()) != std::string::npos);
  CHECK(helper_log.find("waiting for parent pid ") != std::string::npos);
  CHECK(helper_log.find("parent exited") != std::string::npos);
  CHECK(helper_log.find("manifest entries: 1") != std::string::npos);
  CHECK(helper_log.find("backup start") != std::string::npos);
  CHECK(helper_log.find("backup complete") != std::string::npos);
  CHECK(helper_log.find("replacement complete") != std::string::npos);
  CHECK(helper_log.find("verification complete") != std::string::npos);
  CHECK(helper_log.find("restart executable: " + (install / "app.exe").string()) != std::string::npos);
  CHECK(helper_log.find("restart complete") != std::string::npos);
  CHECK(helper_log.find("exit code: 0") != std::string::npos);

  CHECK(!std::filesystem::exists(job));
  CHECK(!std::filesystem::exists(backup));
  CHECK(!std::filesystem::exists(staging));

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
