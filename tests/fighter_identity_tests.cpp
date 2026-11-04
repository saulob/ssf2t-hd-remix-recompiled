#include <cstdio>
#include <string_view>

#include "character/fighter_identity.h"
#include "character/ultra_gameplay.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

}  // namespace

int main() {
  using ssf2t::character::FighterIdentity;
  using ssf2t::character::IdentityForSide;
  using ssf2t::character::IdentityName;
  using ssf2t::character::RequestedIdentity;
  using ssf2t::character::ResetFightIdentities;
  using ssf2t::character::ResetIdentities;
  using ssf2t::character::ResolveFighterIdentity;
  using ssf2t::character::SetIdentityForSide;
  using ssf2t::character::SetRequestedIdentity;

  auto info = ResolveFighterIdentity(0, false, false, FighterIdentity::kEvilRyu);
  Check(info.identity == FighterIdentity::kRyu, "Ryu stays Ryu with the option off");
  Check(!info.ultra_fighters, "option off is not marked");

  info = ResolveFighterIdentity(4, false, false, FighterIdentity::kViolentKen);
  Check(info.identity == FighterIdentity::kKen, "Ken stays Ken with the option off");

  info = ResolveFighterIdentity(0, false, true, FighterIdentity::kEvilRyu);
  Check(info.identity == FighterIdentity::kEvilRyu, "requested Evil Ryu maps from Ryu");
  Check(info.guest_character == 0, "Evil Ryu keeps the guest Ryu id");
  Check(info.ultra_fighters, "option on is marked");

  info = ResolveFighterIdentity(4, false, true, FighterIdentity::kViolentKen);
  Check(info.identity == FighterIdentity::kViolentKen, "requested Violent Ken maps from Ken");
  Check(info.guest_character == 4, "Violent Ken keeps the guest Ken id");

  info = ResolveFighterIdentity(4, false, true, FighterIdentity::kEvilRyu);
  Check(info.identity == FighterIdentity::kKen, "Evil Ryu does not map from Ken");

  info = ResolveFighterIdentity(0, false, true, FighterIdentity::kViolentKen);
  Check(info.identity == FighterIdentity::kRyu, "Violent Ken does not map from Ryu");

  info = ResolveFighterIdentity(0, true, true, FighterIdentity::kEvilRyu);
  Check(info.identity == FighterIdentity::kRyu, "hidden Ryu is not converted");

  info = ResolveFighterIdentity(16, false, true, FighterIdentity::kEvilRyu);
  Check(info.identity == FighterIdentity::kGuest, "Akuma stays a guest identity");

  Check(IdentityName(FighterIdentity::kEvilRyu) == std::string_view("Evil Ryu"), "Evil Ryu name");
  Check(IdentityName(FighterIdentity::kViolentKen) == std::string_view("Violent Ken"), "Violent Ken name");
  Check(IdentityName(FighterIdentity::kRyu) == std::string_view("Ryu"), "Ryu name");
  Check(IdentityName(FighterIdentity::kKen) == std::string_view("Ken"), "Ken name");

  using ssf2t::character::StartedFighterIdentity;
  Check(StartedFighterIdentity(false, 0, false, true, FighterIdentity::kEvilRyu) == FighterIdentity::kGuest,
        "a fighter the guest has not started has no identity, though it reads as Ryu");
  Check(StartedFighterIdentity(true, 0, false, true, FighterIdentity::kEvilRyu) == FighterIdentity::kEvilRyu,
        "a started carrier is Evil Ryu");
  Check(StartedFighterIdentity(true, 4, false, true, FighterIdentity::kEvilRyu) == FighterIdentity::kKen,
        "a started fighter on another character is never Evil Ryu");
  Check(StartedFighterIdentity(true, 0, false, false, FighterIdentity::kEvilRyu) == FighterIdentity::kRyu,
        "Ultra off keeps the carrier ordinary Ryu");
  Check(StartedFighterIdentity(true, 0, false, true, FighterIdentity::kGuest) == FighterIdentity::kRyu,
        "a side that did not pick Evil Ryu stays ordinary Ryu");

  ResetIdentities();
  SetRequestedIdentity(0, FighterIdentity::kEvilRyu);
  Check(StartedFighterIdentity(true, 0, false, true, RequestedIdentity(0)) == FighterIdentity::kEvilRyu,
        "player 1's choice applies to player 1");
  Check(StartedFighterIdentity(true, 0, false, true, RequestedIdentity(1)) == FighterIdentity::kRyu,
        "an ordinary Ryu on the other side is untouched");

  ResetIdentities();
  Check(IdentityForSide(0).identity == FighterIdentity::kGuest, "reset clears player 1");
  Check(RequestedIdentity(0) == FighterIdentity::kGuest, "reset clears requested identity");

  SetRequestedIdentity(0, FighterIdentity::kEvilRyu);
  Check(RequestedIdentity(0) == FighterIdentity::kEvilRyu, "requested identity is stored");

  SetIdentityForSide(0, ResolveFighterIdentity(0, false, true, FighterIdentity::kEvilRyu));
  Check(IdentityForSide(0).identity == FighterIdentity::kEvilRyu, "stored identity");
  Check(IdentityForSide(2).identity == FighterIdentity::kGuest, "invalid side is empty");

  if (failures == 0) std::printf("fighter identity tests passed\n");
  return failures == 0 ? 0 : 1;
}
