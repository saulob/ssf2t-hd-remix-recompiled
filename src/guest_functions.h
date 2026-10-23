#pragma once

struct PPCFuncMapping;

namespace ssf2t {

// Registers the generated guest-to-host function table, so a native crash address can name the recompiled function.
void InstallGuestFunctionTable(const PPCFuncMapping* mappings);

}  // namespace ssf2t
