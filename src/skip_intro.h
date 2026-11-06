#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ssf2t {

// TiXmlDocument::Parse takes the gameflow FSM document. The boot logo step draws the Capcom and Backbone logos, one
// frame at a time, from a three-value step counter until it sets the finished flag.
struct Base013SkipIntro {
  static constexpr const char* kName = "base013";
  static constexpr uint32_t kXmlDocumentParse = 0x82416E80;
  static constexpr uint32_t kBootLogoStep = 0x823ECB08;
  static constexpr uint32_t kBootLogoState = 0x83E381B0;
  static constexpr uint32_t kBootLogoFinished = 0x83E37F66;
  static constexpr uint32_t kCapcomLogo = 0x83BE5AA4;
  static constexpr uint32_t kBackboneLogo = 0x83BE5AA8;
  static constexpr uint32_t kTextureRefCount = 0x82173990;
  static constexpr uint32_t kTextureRelease = 0x82173948;
};

struct TitleUpdate023SkipIntro {
  static constexpr const char* kName = "tu023";
  static constexpr uint32_t kXmlDocumentParse = 0x82417548;
  static constexpr uint32_t kBootLogoStep = 0x823ED1F8;
  static constexpr uint32_t kBootLogoState = 0x83E386C8;
  static constexpr uint32_t kBootLogoFinished = 0x83E3847E;
  static constexpr uint32_t kCapcomLogo = 0x83BE5B24;
  static constexpr uint32_t kBackboneLogo = 0x83BE5B28;
  static constexpr uint32_t kTextureRefCount = 0x821739B0;
  static constexpr uint32_t kTextureRelease = 0x82173968;
};

// Step 0 shows Capcom, 1 shows Backbone, and 2 releases Backbone and finishes the presentation.
inline constexpr uint32_t kBootLogosEndStep = 2;

inline bool SkipsBootLogos(bool setting, uint32_t step, uint8_t finished) {
  return setting && finished == 0 && step < kBootLogosEndStep;
}

inline constexpr std::string_view kGameflowFsmOpen = "<FSM name=\"gameflow_fsm\">";
inline constexpr std::size_t kGameflowHead = 256;

inline constexpr std::string_view kBootState = "<State name=\"gameflow_fsm_load_screenset_i000\">";
inline constexpr std::string_view kIntroState = "<State name=\"gameflow_fsm_LegalText_i000\"";
inline constexpr std::string_view kTitleGateState = "<State name=\"gameflow_fsm_OpenBeta_i000\"";
inline constexpr std::string_view kTitleScreenState = "<State name=\"gameflow_fsm_Title_Screen_i000\"";
inline constexpr std::string_view kIntroDestination = "dst=\"gameflow_fsm_LegalText_i000\"";
inline constexpr std::string_view kTitleGateDestination = "dst=\"gameflow_fsm_OpenBeta_i000\"";

inline bool IsGameflowFsmHead(std::string_view head) { return head.find(kGameflowFsmOpen) != std::string_view::npos; }

// The boot state's only exit leads to the legal screen, held five seconds before OpenBeta and the title screen. Pointing
// it at OpenBeta keeps every state the game runs after the legal screen. A document of another layout returns nothing.
inline std::optional<std::string> PatchGameflowIntro(std::string_view xml) {
  if (!IsGameflowFsmHead(xml.substr(0, kGameflowHead))) {
    return std::nullopt;
  }
  const std::size_t boot = xml.find(kBootState);
  if (boot == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t boot_end = xml.find("</State>", boot);
  const std::size_t destination = xml.find(kIntroDestination, boot);
  if (boot_end == std::string_view::npos || destination == std::string_view::npos || destination > boot_end ||
      xml.find(kIntroDestination, destination + kIntroDestination.size()) < boot_end) {
    return std::nullopt;
  }
  for (const std::string_view state : {kIntroState, kTitleGateState, kTitleScreenState}) {
    if (xml.find(state) == std::string_view::npos) {
      return std::nullopt;
    }
  }
  std::string patched(xml);
  patched.replace(destination, kIntroDestination.size(), kTitleGateDestination);
  return patched;
}

}  // namespace ssf2t
