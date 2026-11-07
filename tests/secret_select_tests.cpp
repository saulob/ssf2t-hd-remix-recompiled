// Secret characters in an Online Match: the AND rule for Akuma and Evil Ryu, the Character Select gating, the host
// selection that rides in the rollback blob, and the validation of a selection against what the match negotiated.
// Headless: no window, no network.
// Run: ssf2t_secret_select_tests

#include <cstdint>
#include <cstdio>
#include <vector>

#include "character/akuma_navigation.h"
#include "character/evil_ryu_moves.h"
#include "character/fighter_identity.h"
#include "character/host_rollback.h"
#include "character/secret_select.h"
#include "character/ultra_navigation.h"
#include "netplay/host_state.h"
#include "online/secret_characters.h"

using namespace ssf2t::character;
using ssf2t::online::EffectiveSecrets;
using ssf2t::online::MatchSecrets;
using ssf2t::online::SecretCapabilities;
using ssf2t::online::SecretCharacter;
using ssf2t::online::SecretSet;
using ssf2t::online::SecretStatus;
using ssf2t::online::SecretStatusOf;
using ssf2t::online::VerifiedSecrets;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

SecretCapabilities Caps(bool akuma, bool evil, bool supports_akuma = true, bool supports_evil = true) {
  SecretCapabilities caps;
  caps.supports = {supports_akuma, supports_evil};
  caps.enabled = {akuma, evil};
  return caps;
}

void TestAkumaNeedsBothPlayers() {
  for (const bool mine : {false, true}) {
    for (const bool theirs : {false, true}) {
      const SecretSet effective = EffectiveSecrets(Caps(mine, false), Caps(theirs, false));
      CHECK(effective.akuma == (mine && theirs));
      CHECK(!effective.evil_ryu);
      CHECK(EffectiveSecrets(Caps(mine, false), Caps(theirs, false)) == EffectiveSecrets(Caps(theirs, false), Caps(mine, false)));
    }
  }
}

void TestEvilRyuNeedsBothPlayers() {
  for (const bool mine : {false, true}) {
    for (const bool theirs : {false, true}) {
      const SecretSet effective = EffectiveSecrets(Caps(false, mine), Caps(false, theirs));
      CHECK(effective.evil_ryu == (mine && theirs));
      CHECK(!effective.akuma);
      CHECK(EffectiveSecrets(Caps(false, mine), Caps(false, theirs)) == EffectiveSecrets(Caps(false, theirs), Caps(false, mine)));
    }
  }
}

void TestEachCharacterIsNegotiatedOnItsOwn() {
  const SecretSet both = EffectiveSecrets(Caps(true, false), Caps(true, true));
  CHECK(both.akuma && !both.evil_ryu);
  const SecretSet other = EffectiveSecrets(Caps(false, true), Caps(true, true));
  CHECK(!other.akuma && other.evil_ryu);
}

void TestMissingSupportMeansUnavailable() {
  const SecretCapabilities old_peer;
  CHECK(!EffectiveSecrets(Caps(true, true), old_peer).Any());
  CHECK(!EffectiveSecrets(old_peer, Caps(true, true)).Any());
  SecretCapabilities enabled_without_support;
  enabled_without_support.enabled = {true, true};
  CHECK(!EffectiveSecrets(Caps(true, true), enabled_without_support).Any());
  CHECK(!enabled_without_support.Normalized().enabled.Any());
  const SecretSet only_akuma = EffectiveSecrets(Caps(true, true), Caps(true, true, true, false));
  CHECK(only_akuma.akuma && !only_akuma.evil_ryu);
}

void TestTheServicesAnswerCanOnlyShrink() {
  MatchSecrets room;
  room.peer = Caps(true, true);
  room.effective = {true, true};
  CHECK(VerifiedSecrets(Caps(true, true), room) == (SecretSet{true, true}));
  CHECK(VerifiedSecrets(Caps(true, false), room) == (SecretSet{true, false}));
  CHECK(VerifiedSecrets(Caps(false, false), room) == (SecretSet{false, false}));
  room.effective = {false, false};
  CHECK(VerifiedSecrets(Caps(true, true), room) == (SecretSet{false, false}));
  room.effective = {true, true};
  room.peer = SecretCapabilities{};
  CHECK(!VerifiedSecrets(Caps(true, true), room).Any());
}

void TestStatusExplainsWhy() {
  const SecretCharacter akuma = SecretCharacter::kAkuma;
  CHECK(SecretStatusOf(akuma, Caps(true, true), Caps(true, true)) == SecretStatus::kAvailable);
  CHECK(SecretStatusOf(akuma, Caps(false, true), Caps(true, true)) == SecretStatus::kDisabledLocally);
  CHECK(SecretStatusOf(akuma, Caps(true, true), Caps(false, true)) == SecretStatus::kDisabledByOpponent);
  CHECK(SecretStatusOf(akuma, Caps(true, true), SecretCapabilities{}) == SecretStatus::kUnsupportedByOpponent);
  CHECK(SecretStatusOf(akuma, Caps(true, true), std::nullopt) == SecretStatus::kWaiting);
  CHECK(SecretStatusOf(akuma, Caps(true, true), Caps(true, true), false) == SecretStatus::kUnsupportedByService);
  CHECK(SecretStatusOf(akuma, Caps(false, true), Caps(true, true), false) == SecretStatus::kDisabledLocally);
  CHECK(SecretStatusOf(SecretCharacter::kEvilRyu, Caps(true, true), Caps(true, false)) == SecretStatus::kDisabledByOpponent);
}

void TestLocalSelectFollowsTheLocalOptionAndOnlineFollowsTheNegotiation() {
  for (const bool option : {false, true}) {
    for (const bool negotiated : {false, true}) {
      CHECK(SecretAvailable(false, option, negotiated) == option);
      CHECK(SecretAvailable(true, option, negotiated) == negotiated);
    }
  }
  CHECK(OnlineSelectMode(3) && !OnlineSelectMode(0) && !OnlineSelectMode(1) && !OnlineSelectMode(2));
}

void TestOnlineAkumaUnlockIgnoresTheSave() {
  CHECK(AkumaUnlockFor(false, false, 0) == 0);
  CHECK(AkumaUnlockFor(false, false, 1) == 1);
  CHECK(AkumaUnlockFor(false, true, 0) == 1);
  CHECK(AkumaUnlockFor(true, true, 0) == 1);
  CHECK(AkumaUnlockFor(true, false, 1) == 0);
  CHECK(AkumaUnlockFor(true, false, 0) == 0);
}

void TestSelectionIsValidatedPerSide() {
  const SecretSet none;
  const SecretSet akuma{true, false};
  const SecretSet evil{false, true};
  CHECK(ValidateSecretSelection(kRyuGuestCharacter, FighterIdentity::kGuest, none) == SelectionVerdict::kAccepted);
  CHECK(ValidateSecretSelection(kKenGuestCharacter, FighterIdentity::kGuest, none) == SelectionVerdict::kAccepted);
  CHECK(ValidateSecretSelection(kAkumaGuestCharacter, FighterIdentity::kGuest, none) == SelectionVerdict::kAkuma);
  CHECK(ValidateSecretSelection(kAkumaGuestCharacter, FighterIdentity::kGuest, evil) == SelectionVerdict::kAkuma);
  CHECK(ValidateSecretSelection(kAkumaGuestCharacter, FighterIdentity::kGuest, akuma) == SelectionVerdict::kAccepted);
  CHECK(ValidateSecretSelection(kRyuGuestCharacter, FighterIdentity::kEvilRyu, none) == SelectionVerdict::kEvilRyu);
  CHECK(ValidateSecretSelection(kRyuGuestCharacter, FighterIdentity::kEvilRyu, akuma) == SelectionVerdict::kEvilRyu);
  CHECK(ValidateSecretSelection(kRyuGuestCharacter, FighterIdentity::kEvilRyu, evil) == SelectionVerdict::kAccepted);
  CHECK(ValidateSecretSelection(kRyuGuestCharacter, FighterIdentity::kGuest, evil) == SelectionVerdict::kAccepted);
  const SecretSet both{true, true};
  CHECK(ValidateSecretSelection(kAkumaGuestCharacter, FighterIdentity::kGuest, both) == SelectionVerdict::kAccepted);
  CHECK(ValidateSecretSelection(kRyuGuestCharacter, FighterIdentity::kEvilRyu, both) == SelectionVerdict::kAccepted);
}

void TestReportsKeepRyuAndEvilRyuApart() {
  CHECK(!ReportsEvilRyu(kRyuGuestCharacter, FighterIdentity::kGuest, true));
  CHECK(!ReportsEvilRyu(kRyuGuestCharacter, FighterIdentity::kRyu, true));
  CHECK(ReportsEvilRyu(kRyuGuestCharacter, FighterIdentity::kEvilRyu, true));
  CHECK(!ReportsEvilRyu(kRyuGuestCharacter, FighterIdentity::kEvilRyu, false));
  CHECK(!ReportsEvilRyu(kKenGuestCharacter, FighterIdentity::kEvilRyu, true));
  CHECK(!ReportsEvilRyu(kAkumaGuestCharacter, FighterIdentity::kEvilRyu, true));
}

// Each side's choice is independent, whichever pairing the two players make.
void TestEveryPairingOfSecretAndNormalFightersIsAccepted() {
  UltraNavigation navigation;
  const SecretSet both{true, true};
  struct Pick {
    int guest;
    bool evil;
  };
  const Pick ryu{kRyuGuestCharacter, false};
  const Pick evil{kRyuGuestCharacter, true};
  const Pick akuma{kAkumaGuestCharacter, false};
  for (const Pick& p1 : {ryu, evil, akuma}) {
    for (const Pick& p2 : {ryu, evil, akuma}) {
      CHECK(ValidateSecretSelection(p1.guest, p1.evil ? FighterIdentity::kEvilRyu : FighterIdentity::kGuest, both) == SelectionVerdict::kAccepted);
      CHECK(ValidateSecretSelection(p2.guest, p2.evil ? FighterIdentity::kEvilRyu : FighterIdentity::kGuest, both) == SelectionVerdict::kAccepted);
    }
  }
  CHECK(ReportsEvilRyu(evil.guest, FighterIdentity::kEvilRyu, true) && !ReportsEvilRyu(ryu.guest, FighterIdentity::kGuest, true));
  CHECK(ValidateSecretSelection(akuma.guest, akuma.evil ? FighterIdentity::kEvilRyu : FighterIdentity::kGuest, SecretSet{false, true}) == SelectionVerdict::kAkuma);
  CHECK(ValidateSecretSelection(evil.guest, evil.evil ? FighterIdentity::kEvilRyu : FighterIdentity::kGuest, SecretSet{true, false}) == SelectionVerdict::kEvilRyu);
  CHECK(!navigation.OnEvilRyu(0));
}

// Right from the cell beside Evil Ryu's enters him; both sides can be on him at once (a mirror).
void TestBothSidesCanSelectEvilRyuIndependently() {
  ResetIdentities();
  UltraNavigation navigation;
  const UltraMove first = navigation.Resolve(true, false, 0, kEvilRyuNeighbourLive, PadDirection::kRight);
  CHECK(first.action == UltraMoveAction::kEnter);
  CHECK(navigation.OnEvilRyu(0) && !navigation.OnEvilRyu(1));
  CHECK(RequestedIdentity(0) == FighterIdentity::kEvilRyu && RequestedIdentity(1) == FighterIdentity::kGuest);
  const UltraMove second = navigation.Resolve(true, false, 1, kEvilRyuNeighbourLive, PadDirection::kRight);
  CHECK(second.action == UltraMoveAction::kEnter);
  CHECK(navigation.OnEvilRyu(0) && navigation.OnEvilRyu(1));
  CHECK(navigation.Mask() == 3);
  navigation.Resolve(true, false, 0, kEvilRyuCarrierLive, PadDirection::kLeft);
  CHECK(!navigation.OnEvilRyu(0) && navigation.OnEvilRyu(1));
  ResetIdentities();
}

// A rollback puts the Evil Ryu cell back as the saved frame had it, so a mispredicted move cannot leave it selected.
void TestRollbackRestoresTheHostSelection() {
  ResetIdentities();
  UltraNavigation ultra;
  AkumaNavigation akuma;
  HostMoves moves;
  std::vector<uint8_t> saved(kHostRollbackBytes);
  SaveHostRollback(ultra, akuma, moves, saved.data());

  ultra.Resolve(true, false, 1, kEvilRyuNeighbourLive, PadDirection::kRight);
  uint8_t parked = 0;
  akuma.Resolve(true, SelectLayout::kVersus, 0, 2, NavDirection::kUp, parked);
  moves.For(0).kind = HostMoveKind::kTeleport;
  moves.For(0).ticks = 17;
  CHECK(ultra.OnEvilRyu(1) && RequestedIdentity(1) == FighterIdentity::kEvilRyu);

  std::vector<uint8_t> mispredicted(kHostRollbackBytes);
  SaveHostRollback(ultra, akuma, moves, mispredicted.data());

  LoadHostRollback(ultra, akuma, moves, saved.data());
  CHECK(!ultra.OnEvilRyu(0) && !ultra.OnEvilRyu(1));
  CHECK(RequestedIdentity(1) == FighterIdentity::kGuest);
  CHECK(akuma.column[0] == kNoColumn && !moves.Active(0));

  LoadHostRollback(ultra, akuma, moves, mispredicted.data());
  CHECK(!ultra.OnEvilRyu(0) && ultra.OnEvilRyu(1));
  CHECK(RequestedIdentity(1) == FighterIdentity::kEvilRyu);
  CHECK(akuma.column[0] != kNoColumn && akuma.layout[0] == SelectLayout::kVersus);
  CHECK(moves.For(0).kind == HostMoveKind::kTeleport && moves.For(0).ticks == 17);

  std::vector<uint8_t> again(kHostRollbackBytes);
  SaveHostRollback(ultra, akuma, moves, again.data());
  CHECK(again[0] == mispredicted[0] && again[1] == mispredicted[1] && again[2] == mispredicted[2]);
  ResetIdentities();
}

void TestAnOnlineSelectStartsFromNothing() {
  ResetIdentities();
  UltraNavigation ultra;
  AkumaNavigation akuma;
  HostMoves moves;
  ultra.Resolve(true, false, 0, kEvilRyuNeighbourLive, PadDirection::kRight);
  moves.For(1).kind = HostMoveKind::kRagingDemon;
  uint8_t next = 0;
  akuma.Resolve(true, SelectLayout::kArcade, 1, 3, NavDirection::kUp, next);
  ResetHostSelect(ultra, akuma, moves);
  CHECK(ultra.Mask() == 0 && RequestedIdentity(0) == FighterIdentity::kGuest);
  CHECK(!moves.Active(1) && akuma.column[1] == kNoColumn);
  ResetIdentities();
}

void TestTheHostStateCodecOnlyAddsBytesOnceInstalled() {
  using namespace ssf2t::netplay;
  const HostStateCodec before = g_host_state;
  SetHostStateCodec({});
  CHECK(HostStateBytes() == 0);
  uint8_t byte = 0xAA;
  SaveHostState(&byte);
  CHECK(byte == 0xAA);
  SetHostStateCodec({uint32_t(kHostRollbackBytes), nullptr, nullptr});
  CHECK(HostStateBytes() == 0);
  SetHostStateCodec({uint32_t(kHostRollbackBytes), [](uint8_t* out) { *out = 7; }, [](const uint8_t*) {}});
  CHECK(HostStateBytes() == kHostRollbackBytes);
  SaveHostState(&byte);
  CHECK(byte == 7);
  SetHostStateCodec(before);
}

}  // namespace

int main() {
  std::printf("secret select tests\n");
  TestAkumaNeedsBothPlayers();
  TestEvilRyuNeedsBothPlayers();
  TestEachCharacterIsNegotiatedOnItsOwn();
  TestMissingSupportMeansUnavailable();
  TestTheServicesAnswerCanOnlyShrink();
  TestStatusExplainsWhy();
  TestLocalSelectFollowsTheLocalOptionAndOnlineFollowsTheNegotiation();
  TestOnlineAkumaUnlockIgnoresTheSave();
  TestSelectionIsValidatedPerSide();
  TestReportsKeepRyuAndEvilRyuApart();
  TestEveryPairingOfSecretAndNormalFightersIsAccepted();
  TestBothSidesCanSelectEvilRyuIndependently();
  TestRollbackRestoresTheHostSelection();
  TestAnOnlineSelectStartsFromNothing();
  TestTheHostStateCodecOnlyAddsBytesOnceInstalled();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
