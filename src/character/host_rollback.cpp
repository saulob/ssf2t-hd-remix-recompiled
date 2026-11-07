#include "character/host_rollback.h"

#include "netplay/host_state.h"

namespace ssf2t::character {
namespace {

void Save(uint8_t* out) {
  SaveHostRollback(GlobalUltraNavigation(), GlobalAkumaNavigation(), GlobalHostMoves(), out);
}

void Load(const uint8_t* in) {
  LoadHostRollback(GlobalUltraNavigation(), GlobalAkumaNavigation(), GlobalHostMoves(), in);
}

[[maybe_unused]] const bool g_installed = (InstallHostRollback(), true);

}  // namespace

void InstallHostRollback() {
  netplay::SetHostStateCodec({uint32_t(kHostRollbackBytes), Save, Load});
}

void ResetOnlineSelectState() {
  ResetHostSelect(GlobalUltraNavigation(), GlobalAkumaNavigation(), GlobalHostMoves());
}

}  // namespace ssf2t::character
