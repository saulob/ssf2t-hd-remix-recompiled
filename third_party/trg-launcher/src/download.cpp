#include "trg/download.h"

#include <cstdio>

#include "trg/platform.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace trg {

#if defined(_WIN32)
std::string HttpGet(const std::string& url, std::string& out, Task* task) {
  out.clear();
  const int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
  std::wstring wurl(wlen > 0 ? size_t(wlen - 1) : 0, L'\0');
  if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);
  URL_COMPONENTSW parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256], path[4096];
  parts.lpszHostName = host;
  parts.dwHostNameLength = 256;
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = 4096;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) return "Not a valid web address.";
  HINTERNET session = WinHttpOpen(L"TRG-Launcher/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return "Could not start the download.";
  std::string error = "Could not reach the server. Check your internet connection and try again.";
  if (HINTERNET conn = WinHttpConnect(session, host, parts.nPort, 0)) {
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    if (HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags)) {
      DWORD code = 0, len = sizeof(code);
      if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(req, nullptr) &&
          WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &len,
                              nullptr)) {
        if (code != 200) {
          error = "The server answered " + std::to_string(code) + ".";
        } else {
          DWORD content = 0;
          len = sizeof(content);
          const bool sized = WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                                 nullptr, &content, &len, nullptr);
          error.clear();
          for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail;) {
            if (task && task->cancelled()) {
              error = "Cancelled.";
              break;
            }
            const size_t at = out.size();
            out.resize(at + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, out.data() + at, avail, &read)) {
              error = "The download was interrupted.";
              break;
            }
            out.resize(at + read);
            if (task) task->Progress((long long)out.size(), sized ? (long long)content : -1);
          }
        }
      }
      WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
  }
  WinHttpCloseHandle(session);
  return error;
}
#else
std::string HttpGet(const std::string& url, std::string& out, Task* task) {
  out.clear();
  // Only plain web addresses reach the shell (no quotes or other specials).
  for (char c : url)
    if (c == '\'' || c == '"' || c == '\\' || c == '`' || c == '$' || (unsigned char)c < 0x21)
      return "Not a valid web address.";
  FILE* p = popen(("curl -fsSL '" + url + "'").c_str(), "r");
  if (!p) return "curl is needed to download. Install it with your package manager.";
  char buf[1 << 16];
  for (size_t n; (n = fread(buf, 1, sizeof(buf), p)) > 0;) {
    out.append(buf, n);
    if (task) {
      task->Progress((long long)out.size(), -1);
      if (task->cancelled()) {
        pclose(p);
        return "Cancelled.";
      }
    }
  }
  const int status = pclose(p);
  if (status != 0) return "The download failed. Check your internet connection and try again.";
  return "";
}
#endif

std::string DownloadFile(Task& task, const std::string& url, const std::string& utf8_path) {
  std::string data;
  if (std::string error = HttpGet(url, data, &task); !error.empty()) return error;
  const std::string part = utf8_path + ".part";
  FILE* f = OpenFile(part, "wb");
  if (!f) return "Cannot write " + utf8_path + ".";
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  if (!ok || !MoveFileOver(part, utf8_path)) {
    RemoveFile(part);
    return "Cannot write " + utf8_path + ".";
  }
  return "";
}

}  // namespace trg
