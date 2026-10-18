// UTF-8 strings <-> std::filesystem::path, the same under C++17 and C++20.
#pragma once

#include <filesystem>
#include <string>

namespace trg::detail {

inline std::filesystem::path Utf8Path(const std::string& s) {
#if defined(__cpp_char8_t)
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
  return std::filesystem::u8path(s);
#endif
}

inline std::string ToUtf8(const std::filesystem::path& p) {
  const auto s = p.u8string();  // std::string in C++17, std::u8string in C++20
  return std::string(s.begin(), s.end());
}

}  // namespace trg::detail
