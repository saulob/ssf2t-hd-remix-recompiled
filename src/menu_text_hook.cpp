#include "menu_text.h"

#include <rex/hook.h>

// The string table splitter: r3 is the cursor, r4 the end of the buffer. The span up to the next LF holds whole entries.
REX_EXTERN(__imp__sub_82439FA0);
REX_HOOK_RAW(sub_82439FA0) {
  if (ctx.r4.u32 > ctx.r3.u32) {
    char* data = reinterpret_cast<char*>(base + ctx.r3.u32);
    const size_t length = ctx.r4.u32 - ctx.r3.u32;
    size_t span = 0;
    while (span < length && data[span] != '\n') ++span;
    if (span < length) ++span;
    ssf2t::PatchMainMenuExitBuffer(data, span);
  }
  __imp__sub_82439FA0(ctx, base);
}
