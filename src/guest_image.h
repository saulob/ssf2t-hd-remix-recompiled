#pragma once

#include <rex/image_info.h>

#include "game_files.h"

namespace ssf2t {

// The layout and function table of the recompiled image for `guest`.
const rex::PPCImageInfo& GuestImageConfig(GuestVersion guest);

}  // namespace ssf2t
