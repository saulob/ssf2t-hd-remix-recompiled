#include "guest_image.h"

extern const rex::PPCImageInfo base013_PPCImageConfig;
#if SSF2T_TITLE_UPDATE_IMAGE
extern const rex::PPCImageInfo tu023_PPCImageConfig;
#endif

namespace ssf2t {

const rex::PPCImageInfo& GuestImageConfig(GuestVersion guest) {
#if SSF2T_TITLE_UPDATE_IMAGE
  if (guest == GuestVersion::kTitleUpdate023) {
    return tu023_PPCImageConfig;
  }
#endif
  return base013_PPCImageConfig;
}

}  // namespace ssf2t
