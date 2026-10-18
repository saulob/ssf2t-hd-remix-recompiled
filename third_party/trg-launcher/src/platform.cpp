#include "trg/platform.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace trg {
namespace {

#if defined(_WIN32)
std::wstring Widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
  return w;
}

std::string Narrow(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string s(size_t(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
#else
std::string ShellQuote(const std::string& s) {
  std::string q = "'";
  for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return q + "'";
}

// Runs a dialog command and returns its first line of output. `ran` is set
// when the program exists, even if the player cancelled.
std::string RunDialog(const std::string& cmd, bool& ran) {
  ran = false;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return {};
  char buf[4096] = "";
  const bool got = fgets(buf, sizeof buf, p) != nullptr;
  const int rc = pclose(p);
  std::string s = buf;
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  ran = rc == 0 || rc == 256;  // 1 = cancelled; 127 = not installed
  return got && rc == 0 ? s : std::string();
}
#endif

}  // namespace

// -------------------------------------------------------------------- files --
FILE* OpenFile(const std::string& utf8_path, const char* mode) {
#if defined(_WIN32)
  return _wfopen(Widen(utf8_path).c_str(), Widen(mode).c_str());
#else
  return fopen(utf8_path.c_str(), mode);
#endif
}

long long FileSize(const std::string& utf8_path) {
  std::error_code ec;
  const auto n = std::filesystem::file_size(std::filesystem::u8path(utf8_path), ec);
  return ec ? -1 : (long long)n;
}

bool RemoveFile(const std::string& utf8_path) {
  std::error_code ec;
  return std::filesystem::remove(std::filesystem::u8path(utf8_path), ec);
}

bool MoveFileOver(const std::string& from, const std::string& to) {
#if defined(_WIN32)
  return MoveFileExW(Widen(from).c_str(), Widen(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(from.c_str(), to.c_str()) == 0;
#endif
}

std::string AbsolutePath(const std::string& utf8_path) {
  std::error_code ec;
  auto p = std::filesystem::absolute(std::filesystem::u8path(utf8_path), ec);
  if (ec) return utf8_path;
  auto s = p.lexically_normal().u8string();
  std::string out(s.begin(), s.end());
  for (char& c : out)
    if (c == '\\') c = '/';
  return out;
}

// ------------------------------------------------------------------ dialogs --
std::string BrowseForFile(const std::string& title, const std::vector<FileFilter>& filters) {
#if defined(_WIN32)
  // comdlg32 is loaded on first use so games need not link it.
  using GetOpenFileNameWFn = BOOL(WINAPI*)(LPOPENFILENAMEW);
  static HMODULE dlg = LoadLibraryW(L"comdlg32.dll");
  const auto open = dlg ? reinterpret_cast<GetOpenFileNameWFn>(GetProcAddress(dlg, "GetOpenFileNameW")) : nullptr;
  if (!open) return {};
  std::wstring filter;
  for (const FileFilter& f : filters) {
    filter += Widen(f.name + " (" + f.patterns + ")");
    filter.push_back(L'\0');
    filter += Widen(f.patterns);
    filter.push_back(L'\0');
  }
  filter += L"All files";
  filter.push_back(L'\0');
  filter += L"*.*";
  filter.push_back(L'\0');
  filter.push_back(L'\0');
  const std::wstring wtitle = Widen(title);
  wchar_t file[4096] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = file;
  ofn.nMaxFile = DWORD(std::size(file));
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  return open(&ofn) ? Narrow(file) : std::string();
#elif defined(__APPLE__)
  bool ran = false;
  return RunDialog("osascript -e " + ShellQuote("POSIX path of (choose file with prompt \"" + title + "\")") +
                       " 2>/dev/null",
                   ran);
#else
  std::string zenity = "zenity --file-selection --title=" + ShellQuote(title);
  std::string kpatterns;
  for (const FileFilter& f : filters) {
    std::string pats = f.patterns;
    for (char& c : pats)
      if (c == ';') c = ' ';
    zenity += " --file-filter=" + ShellQuote(f.name + " | " + pats);
    kpatterns += (kpatterns.empty() ? "" : " ") + pats;
  }
  zenity += " --file-filter=" + ShellQuote("All files | *") + " 2>/dev/null";
  bool ran = false;
  std::string s = RunDialog(zenity, ran);
  if (ran) return s;
  return RunDialog("kdialog --title " + ShellQuote(title) + " --getopenfilename . " +
                       ShellQuote(kpatterns.empty() ? "*" : kpatterns) + " 2>/dev/null",
                   ran);
#endif
}

std::string BrowseForFolder(const std::string& title) {
#if defined(_WIN32)
  const std::wstring wtitle = Widen(title);
  BROWSEINFOW bi{};
  bi.hwndOwner = GetActiveWindow();
  bi.lpszTitle = wtitle.c_str();
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return {};
  wchar_t path[MAX_PATH] = L"";
  const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
  CoTaskMemFree(pidl);
  return ok ? Narrow(path) : std::string();
#elif defined(__APPLE__)
  bool ran = false;
  return RunDialog("osascript -e " + ShellQuote("POSIX path of (choose folder with prompt \"" + title + "\")") +
                       " 2>/dev/null",
                   ran);
#else
  bool ran = false;
  std::string s = RunDialog("zenity --file-selection --directory --title=" + ShellQuote(title) + " 2>/dev/null", ran);
  if (ran) return s;
  return RunDialog("kdialog --title " + ShellQuote(title) + " --getexistingdirectory . 2>/dev/null", ran);
#endif
}

void OpenInFileManager(const std::string& utf8_path) {
  std::error_code ec;
  const auto p = std::filesystem::u8path(utf8_path);
  const bool is_file = std::filesystem::is_regular_file(p, ec);
#if defined(_WIN32)
  if (is_file) {
    const std::wstring args = L"/select,\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
  } else {
    ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
#else
  const std::string target = is_file ? p.parent_path().u8string() : utf8_path;
#if defined(__APPLE__)
  const std::string cmd = (is_file ? "open -R " + ShellQuote(utf8_path) : "open " + ShellQuote(target)) + " &";
#else
  const std::string cmd = "xdg-open " + ShellQuote(target) + " >/dev/null 2>&1 &";
#endif
  if (std::system(cmd.c_str()) != 0) {
  }
#endif
}

void OpenUrl(const std::string& url) {
#if defined(_WIN32)
  ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#if defined(__APPLE__)
  const std::string cmd = "open " + ShellQuote(url) + " &";
#else
  const std::string cmd = "xdg-open " + ShellQuote(url) + " >/dev/null 2>&1 &";
#endif
  if (std::system(cmd.c_str()) != 0) {
  }
#endif
}

bool ShiftHeld() {
#if defined(_WIN32)
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
  return false;  // no global key state without a window; the SDL host checks its own
#endif
}

// --------------------------------------------------------------------- Task --
Task::~Task() {
  cancel_ = true;
  if (worker_.joinable()) worker_.join();
}

bool Task::Start(std::string label, Job job) {
  if (running()) return false;
  if (worker_.joinable()) worker_.join();
  {
    std::lock_guard<std::mutex> lock(mu_);
    label_ = std::move(label);
    error_.clear();
  }
  cancel_ = false;
  done_ = 0;
  total_ = 0;
  state_ = State::kRunning;
  worker_ = std::thread([this, job = std::move(job)] {
    std::string err;
    try {
      err = job(*this);
    } catch (const std::exception& e) {
      err = e.what();
    } catch (...) {
      err = "Unexpected error";
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      error_ = err;
    }
    state_ = cancel_ ? State::kCancelled : err.empty() ? State::kDone : State::kFailed;
  });
  return true;
}

void Task::SetLabel(std::string label) {
  std::lock_guard<std::mutex> lock(mu_);
  label_ = std::move(label);
}

std::string Task::label() const {
  std::lock_guard<std::mutex> lock(mu_);
  return label_;
}

float Task::fraction() const {
  const long long total = total_;
  if (total <= 0) return -1.0f;
  return float(double(done_) / double(total));
}

Task::State Task::Finish(std::string* error) {
  const State s = state_;
  if (s == State::kIdle || s == State::kRunning) return State::kIdle;
  if (worker_.joinable()) worker_.join();
  if (error) {
    std::lock_guard<std::mutex> lock(mu_);
    *error = error_;
  }
  state_ = State::kIdle;
  return s;
}

std::string CopyFileJob(Task& task, const std::string& from, const std::string& to) {
  const std::string part = to + ".part";
  FILE* in = OpenFile(from, "rb");
  if (!in) return "Cannot read " + from;
  FILE* out = OpenFile(part, "wb");
  if (!out) {
    fclose(in);
    return "Cannot write to " + part;
  }
  const long long total = FileSize(from);
  long long copied = 0;
  std::vector<char> buf(8 << 20);
  std::string err;
  size_t n;
  while (!task.cancelled() && (n = fread(buf.data(), 1, buf.size(), in)) > 0) {
    if (fwrite(buf.data(), 1, n, out) != n) {
      err = "Writing failed: is the disk full?";
      break;
    }
    copied += (long long)n;
    task.Progress(copied, total);
  }
  if (err.empty() && ferror(in)) err = "Reading " + from + " failed";
  fclose(in);
  if (fclose(out) != 0 && err.empty()) err = "Writing failed: is the disk full?";
  if (!err.empty() || task.cancelled()) {
    RemoveFile(part);
    return err;
  }
  if (!MoveFileOver(part, to)) {
    RemoveFile(part);
    return "Cannot move the copy into place at " + to;
  }
  return {};
}

}  // namespace trg
