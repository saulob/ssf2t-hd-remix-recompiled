#include <string>

#include <rex/cvar.h>

#include "aspect_ratio.h"
#include "settings.h"

namespace ssf2t {

void ApplyAspectRatio() {
  const AspectRatio aspect = ParseAspectRatio(rex::cvar::GetFlagByName("hcu_aspect_ratio"));
  rex::cvar::SetFlagByName("video_mode_width", std::to_string(VideoModeWidth(aspect)));
  rex::cvar::SetFlagByName("video_mode_height", std::to_string(kVideoModeHeight));
  rex::cvar::SetFlagByName("present_letterbox", "true");
}

}  // namespace ssf2t
