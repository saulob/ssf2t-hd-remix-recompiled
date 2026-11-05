#include "character/ultra_ending.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

namespace ssf2t::character {
namespace {

constexpr size_t npos = std::string::npos;
constexpr std::string_view kAnimationEnd = "</UIElementDescriptors>";
constexpr std::string_view kFadeIn = "end_short_fadein";
constexpr std::string_view kFadeOut = "end_short_fadeout";
constexpr float kFadeInSeconds = 0.4f;
constexpr float kFadeOutSeconds = 0.6f;
// Native timing around a line: it appears just after the cut and is gone before the next line starts.
constexpr float kLineInDelay = 0.15f;
constexpr float kLineOutLead = 0.15f;
constexpr float kDoneLead = 0.1f;
constexpr char kArtMagic[] = "SSF2VKE1";
constexpr size_t kArtHeader = 12;

std::mutex g_art_mutex;
std::filesystem::path g_art_directory;

std::string Name(const UltraEnding& e, std::string_view tail) { return std::string(e.group) + "_" + std::string(tail); }

std::string Time(float seconds) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.3f", double(seconds));
  return text;
}

std::string AnimEvent(std::string_view animation, const std::string& event) {
  return "<animEvent>\n<anim>" + std::string(animation) + "</anim>\n<event>" + event + "</event>\n</animEvent>\n";
}

std::string Fades(const std::string& prefix) {
  return AnimEvent(kFadeIn, prefix + "fadein") + AnimEvent(kFadeOut, prefix + "fadeout");
}

std::string Frame(const UltraEnding& e, int index) {
  char depth[16];
  std::snprintf(depth, sizeof(depth), "%.1f", 2.1 + 0.1 * index);
  return "<UIGraphic>\n<name>" + EndingFrameName(e, index) +
         "</name>\n<descName>endcinematic_pic</descName>\n<pos>380 290 " + depth + "</pos>\n<color>1 1 1 0</color>\n" +
         Fades(Name(e, "fm" + std::to_string(index + 1))) + "</UIGraphic>\n";
}

std::string Line(const UltraEnding& e, int index) {
  return "<UIText>\n<name>" + Name(e, "text" + std::to_string(index + 1)) +
         "</name>\n<descName>ending_text</descName>\n<pos>386 730 1.500000</pos>\n<size>45 50 1</size>\n"
         "<color>0.274510 0.349020 0.537255 0</color>\n" +
         Fades(Name(e, "tx" + std::to_string(index + 1))) +
         "<fontType>Rf</fontType>\n<font>HelveticaBoldItalic60</font>\n<textID>" + std::string(e.line_ids[index]) +
         "</textID>\n<wrapLength>1500</wrapLength>\n</UIText>\n";
}

std::string EventKey(float seconds, const std::string& event) {
  return "<UIKeyFrame>\n<time>" + Time(seconds) + "</time>\n<eventName>" + event + "</eventName>\n</UIKeyFrame>\n";
}

}  // namespace

std::string EndingFrameName(const UltraEnding& ending, int index) {
  return Name(ending, "frame" + std::to_string(index + 1));
}

bool ParseUltraEndingArt(const uint8_t* file, size_t size, UltraEndingArt* out) {
  if (size != kArtHeader + kEndingCutBytes * kEndingCuts || std::memcmp(file, kArtMagic, 8) != 0) return false;
  if (file[8] != kEndingCuts || file[9] != 0 || file[10] != 0 || file[11] != 0) return false;
  out->pixels.assign(file + kArtHeader, file + size);
  return true;
}

void SetUltraEndingDirectory(const std::filesystem::path& directory) {
  std::lock_guard lock(g_art_mutex);
  g_art_directory = directory;
}

const UltraEndingArt* LoadUltraEndingArt(const UltraEnding& ending) {
  struct Slot {
    UltraEndingArt art;
    bool attempted = false;
    bool loaded = false;
  };
  static std::array<Slot, kUltraEndings.size()> slots;
  size_t index = 0;
  while (index < kUltraEndings.size() && kUltraEndings[index]->identity != ending.identity) ++index;
  if (index == kUltraEndings.size()) return nullptr;
  Slot& slot = slots[index];
  std::lock_guard lock(g_art_mutex);
  if (!slot.attempted) {
    slot.attempted = true;
    std::ifstream in(g_art_directory / ending.file, std::ios::binary);
    std::vector<uint8_t> bytes(kArtHeader + kEndingCutBytes * kEndingCuts);
    slot.loaded = in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) &&
                  in.peek() == std::ifstream::traits_type::eof() && ParseUltraEndingArt(bytes.data(), bytes.size(), &slot.art);
  }
  return slot.loaded ? &slot.art : nullptr;
}

bool PatchEndingScreen(std::string& xml, const UltraEnding& e) {
  const size_t screen = xml.find(e.screen_tag);
  if (screen == npos || xml.find(e.carrier_group_tag, screen) == npos || xml.find(e.group) != npos) return false;
  const size_t elements = xml.find("</uiElements>", screen);
  if (elements == npos) return false;
  std::string group = "<UIElementGroup>\n<name>" + std::string(e.group) + "</name>\n" +
                      AnimEvent(e.group, std::string(e.group));
  for (int cut = 0; cut < kEndingCuts; ++cut) group += Frame(e, cut);
  for (int line = 0; line < e.lines; ++line) group += Line(e, line);
  group += "</UIElementGroup>\n";
  xml.insert(elements, group);
  return true;
}

bool PatchEndingAnimations(std::string& xml, const UltraEnding& e) {
  if (xml.find(e.carrier_group_tag) == npos || xml.find(std::string("<name>") + std::string(kFadeIn) + "</name>") == npos ||
      xml.find(e.group) != npos) {
    return false;
  }
  const size_t end = xml.rfind(kAnimationEnd);
  if (end == npos) return false;
  std::string keys;
  for (int cut = 0; cut < kEndingCuts; ++cut) {
    const float start = e.cut_seconds[size_t(cut)];
    keys += EventKey(start, Name(e, "fm" + std::to_string(cut + 1)) + "fadein");
    const bool last = cut + 1 == kEndingCuts;
    const float out = last ? e.seconds - kFadeOutSeconds - kDoneLead : e.cut_seconds[size_t(cut + 1)] + kFadeInSeconds;
    keys += EventKey(out, Name(e, "fm" + std::to_string(cut + 1)) + "fadeout");
  }
  for (int line = 0; line < e.lines; ++line) {
    const float start = float(line) * kEndingLineSeconds;
    const std::string prefix = Name(e, "tx" + std::to_string(line + 1));
    keys += EventKey(start + kLineInDelay, prefix + "fadein");
    keys += EventKey(start + kEndingLineSeconds - kFadeOutSeconds - kLineOutLead, prefix + "fadeout");
  }
  keys += "<UIKeyFrame>\n<time>" + Time(e.seconds - kDoneLead) +
          "</time>\n<scriptName>GameFlowSignal(cinematic_done);</scriptName>\n</UIKeyFrame>\n";
  const std::string group(e.group);
  xml.insert(end, "<UIAnim>\n<name>" + group + "</name>\n<totalTime>" + Time(e.seconds) +
                      "</totalTime>\n<defaultUIElement>" + group + "</defaultUIElement>\n<UIKeyFrames>\n" + keys +
                      "</UIKeyFrames>\n</UIAnim>\n");
  return true;
}

bool PatchEndingScreen(std::string& xml) {
  bool patched = false;
  for (const UltraEnding* ending : kUltraEndings) patched = PatchEndingScreen(xml, *ending) || patched;
  return patched;
}

bool PatchEndingAnimations(std::string& xml) {
  bool patched = false;
  for (const UltraEnding* ending : kUltraEndings) patched = PatchEndingAnimations(xml, *ending) || patched;
  return patched;
}

}  // namespace ssf2t::character
