#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace local_player_name {

inline constexpr size_t kMaxBytes = 15;
inline constexpr size_t kMaxCharacters = 15;

enum class Result { Ok, Empty, TooManyCharacters, TooManyBytes, BadCharacter };

struct Checked {
  Result result = Result::Empty;
  std::string name;
};

inline size_t Utf8Length(std::string_view s) {
  const auto b = [&](size_t i) { return static_cast<uint8_t>(s[i]); };
  const auto cont = [&](size_t i) { return (b(i) & 0xC0) == 0x80; };
  if (s.empty()) {
    return 0;
  }
  if (b(0) < 0x80) {
    return 1;
  }
  if (b(0) >= 0xC2 && b(0) <= 0xDF) {
    return s.size() >= 2 && cont(1) ? 2 : 0;
  }
  if (b(0) >= 0xE0 && b(0) <= 0xEF) {
    if (s.size() < 3 || !cont(1) || !cont(2) || (b(0) == 0xE0 && b(1) < 0xA0) || (b(0) == 0xED && b(1) >= 0xA0)) {
      return 0;
    }
    return 3;
  }
  if (b(0) >= 0xF0 && b(0) <= 0xF4) {
    if (s.size() < 4 || !cont(1) || !cont(2) || !cont(3) || (b(0) == 0xF0 && b(1) < 0x90) ||
        (b(0) == 0xF4 && b(1) >= 0x90)) {
      return 0;
    }
    return 4;
  }
  return 0;
}

inline Result Measure(std::string_view raw) {
  size_t characters = 0;
  for (size_t i = 0; i < raw.size(); characters++) {
    const size_t length = Utf8Length(raw.substr(i));
    if (!length) {
      return Result::BadCharacter;
    }
    const auto lead = static_cast<uint8_t>(raw[i]);
    const bool c1_control = lead == 0xC2 && static_cast<uint8_t>(raw[i + 1]) < 0xA0;
    if (lead < 0x20 || lead == 0x7F || c1_control) {
      return Result::BadCharacter;
    }
    i += length;
  }
  if (characters > kMaxCharacters) {
    return Result::TooManyCharacters;
  }
  return raw.size() > kMaxBytes ? Result::TooManyBytes : Result::Ok;
}

inline Checked Check(std::string_view raw) {
  while (!raw.empty() && raw.front() == ' ') {
    raw.remove_prefix(1);
  }
  while (!raw.empty() && raw.back() == ' ') {
    raw.remove_suffix(1);
  }
  if (raw.empty()) {
    return {Result::Empty, {}};
  }
  const Result measured = Measure(raw);
  return {measured, measured == Result::Ok ? std::string(raw) : std::string()};
}

// The text after an edit: the proposed text when it is allowed, otherwise the previous text. An empty
// proposal is allowed so the field can be cleared.
inline std::string AcceptEdit(std::string_view previous, std::string_view proposed, Result* blocked = nullptr) {
  const Result result = proposed.empty() ? Result::Ok : Measure(proposed);
  if (blocked) {
    *blocked = result;
  }
  return std::string(result == Result::Ok ? proposed : previous);
}

// The value for the SDK's local_player_N_name cvar: empty keeps the stock name, which a default or an
// invalid name selects.
inline std::string SdkValue(std::string_view raw, std::string_view default_name) {
  const Checked checked = Check(raw);
  return checked.result == Result::Ok && checked.name != default_name ? checked.name : std::string();
}

inline const char* Message(Result result) {
  switch (result) {
    case Result::Empty:
      return "Enter a name.";
    case Result::TooManyCharacters:
      return "Maximum 15 characters.";
    case Result::TooManyBytes:
      return "Name is too long for the game (15-byte limit).";
    case Result::BadCharacter:
      return "That name has a character that cannot be used.";
    default:
      return "";
  }
}

}  // namespace local_player_name
