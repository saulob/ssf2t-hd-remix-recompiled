#include "trg/game_helpers.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#endif

namespace trg {
namespace {

// Average real length of a 1 ms sleep, in milliseconds.
double MeasureSleep1() {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  for (int i = 0; i < 10; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / 10;
}

#if defined(_WIN32)
wchar_t g_crash_dir[MAX_PATH];
wchar_t g_game_name[128];
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
volatile LONG g_crashing = 0;

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
  if (InterlockedExchange(&g_crashing, 1) == 0 && g_crash_dir[0]) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t base[MAX_PATH + 64];
    swprintf(base, MAX_PATH + 64, L"%ls\\crash-%04u%02u%02u-%02u%02u%02u", g_crash_dir, t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond);
    wchar_t path[MAX_PATH + 80];
    swprintf(path, MAX_PATH + 80, L"%ls.dmp", base);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                        MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), &mei, nullptr,
                        nullptr);
      CloseHandle(file);
    }
    swprintf(path, MAX_PATH + 80, L"%ls.txt", base);
    if (FILE* f = _wfopen(path, L"w")) {
      const EXCEPTION_RECORD* rec = info->ExceptionRecord;
      HMODULE module = nullptr;
      wchar_t name[MAX_PATH] = L"?";
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<LPCWSTR>(rec->ExceptionAddress), &module))
        GetModuleFileNameW(module, name, MAX_PATH);
      std::fwprintf(f,
                    L"%ls crashed.\nException 0x%08X at %p (%ls + 0x%llX)\nThread %lu\n"
                    L"Please attach this file and the .dmp file next to it to a bug report.\n",
                    g_game_name, rec->ExceptionCode, rec->ExceptionAddress, name,
                    static_cast<unsigned long long>(static_cast<const char*>(rec->ExceptionAddress) -
                                                    reinterpret_cast<const char*>(module)),
                    GetCurrentThreadId());
      std::fclose(f);
    }
  }
  return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

std::wstring Wide(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? size_t(n - 1) : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}
#endif

}  // namespace

std::string TuneProcessScheduling() {
#if defined(_WIN32)
  const double before = MeasureSleep1();
  const bool timer = timeBeginPeriod(1) == TIMERR_NOERROR;  // held until exit
  PROCESS_POWER_THROTTLING_STATE throttling{};
  throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  throttling.StateMask = 0;  // opt out of both
  const bool power = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
  char line[160];
  std::snprintf(line, sizeof(line), "1 ms sleep took %.1f ms, now %.1f ms (timer %s, power throttling off %s)", before,
                MeasureSleep1(), timer ? "1 ms" : "unchanged", power ? "yes" : "no");
  return line;
#else
  char line[64];
  std::snprintf(line, sizeof(line), "1 ms sleep takes %.1f ms", MeasureSleep1());
  return line;
#endif
}

void InstallCrashReports(const std::string& utf8_logs_dir, const std::string& game_name) {
#if defined(_WIN32)
  const std::wstring dir = Wide(utf8_logs_dir);
  CreateDirectoryW(dir.c_str(), nullptr);
  wcsncpy_s(g_crash_dir, dir.c_str(), _TRUNCATE);
  wcsncpy_s(g_game_name, Wide(game_name).c_str(), _TRUNCATE);
  g_previous = SetUnhandledExceptionFilter(OnCrash);
#else
  (void)utf8_logs_dir;
  (void)game_name;
#endif
}

std::optional<std::string> FrameTimeStats::Add(double frame_ms) {
  times_.push_back(frame_ms);
  elapsed_ms_ += frame_ms;
  if (elapsed_ms_ < window_ * 1000.0) return std::nullopt;
  std::sort(times_.begin(), times_.end());
  const size_t n = times_.size();
  double sum = 0, low_sum = 0;
  for (double t : times_) sum += t;
  const size_t low_n = std::max<size_t>(1, n / 100);
  for (size_t i = n - low_n; i < n; ++i) low_sum += times_[i];
  const auto over = [&](double ms) { return size_t(times_.end() - std::upper_bound(times_.begin(), times_.end(), ms)); };
  char line[200];
  std::snprintf(line, sizeof(line),
                "Frame times: %.1f FPS average, %.1f FPS 1%% low, worst %.0f ms, %zu over 50 ms, %zu over 100 ms",
                1000.0 * double(n) / sum, 1000.0 * double(low_n) / low_sum, times_.back(), over(50.0), over(100.0));
  times_.clear();
  elapsed_ms_ = 0;
  return std::string(line);
}

}  // namespace trg
