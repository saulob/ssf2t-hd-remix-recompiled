#pragma once

namespace ssf2t {

// The launcher's Letterbox / Stretch choice, stored as the ReXGlue cvar present_letterbox.
inline constexpr char kLetterboxSetting[] = "present_letterbox";
inline constexpr char kPresentationLabel[] = "Presentation";
inline constexpr char kPresentationHelp[] =
    "Controls how the rendered image fits a window or display with a different aspect ratio.";

// Segment 0 is Letterbox and segment 1 is Stretch.
constexpr int LetterboxSegment(bool letterbox) {
  return letterbox ? 0 : 1;
}

constexpr bool LetterboxFromSegment(int segment) {
  return segment == 0;
}

}  // namespace ssf2t
