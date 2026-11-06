#pragma once

#include <cstdint>

namespace ssf2t::character {

// The guest functions the character module hooks, per build, so the parity suites can prove each pair on the real
// images. REX_HOOK_RAW takes the address in its symbol name; nothing reads these at run time.

struct Base013UltraHooks {
  static constexpr uint32_t kTrackHalfCircle1 = 0x82264F78;
  static constexpr uint32_t kUnlockAkuma1 = 0x823C9668;
  static constexpr uint32_t kNavigateSelect1 = 0x82243D78;
  static constexpr uint32_t kPlayerCharacterIndex1 = 0x823C8E80;
  static constexpr uint32_t kAnnounceWinnerName1 = 0x82342148;
  static constexpr uint32_t kLoadSounds1 = 0x823D5D18;
  static constexpr uint32_t kRequestSound1 = 0x82375C60;
  static constexpr uint32_t kRoundEndScream1 = 0x8223CDD0;
  static constexpr uint32_t kObserveLock1 = 0x82246D38;
  static constexpr uint32_t kPlaceCursor1 = 0x8232DF78;
  static constexpr uint32_t kPlayEnding1 = 0x823CBE80;
  static constexpr uint32_t kGateSuper1 = 0x8238D3A8;
  static constexpr uint32_t kDrawPortraits1 = 0x82179ED8;
  static constexpr uint32_t kDrawRoster1 = 0x82177728;
  static constexpr uint32_t kDrawSprite1 = 0x8217AC00;
  static constexpr uint32_t kUpdateHud1 = 0x823D01A0;
  static constexpr uint32_t kEmitSprites1 = 0x823A03A0;
  static constexpr uint32_t kEmitSprites2 = 0x823A0660;
  static constexpr uint32_t kEmitSprites3 = 0x823A2F58;
  static constexpr uint32_t kEmitSprites4 = 0x823A4860;
  static constexpr uint32_t kEmitSprites5 = 0x823A5DF8;
  static constexpr uint32_t kHeavyPunch1 = 0x8225E1F0;
  static constexpr uint32_t kLoadGrabRange1 = 0x821C1E70;
  static constexpr uint32_t kLookupAction1 = 0x821C01F8;
  static constexpr uint32_t kRunKenSpecialClass1 = 0x8228E488;
  static constexpr uint32_t kRunSpecialClass1 = 0x82265040;
  static constexpr uint32_t kSelectCpuMove1 = 0x82195568;
  static constexpr uint32_t kStartJump1 = 0x821B41F8;
  static constexpr uint32_t kStartJump2 = 0x8218BB30;
  static constexpr uint32_t kTrackKenKickCommand1 = 0x8228D890;
  static constexpr uint32_t kTrackKenKickCommand2 = 0x8228D8F8;
  static constexpr uint32_t kTrackKenKickCommand3 = 0x8228D960;
  static constexpr uint32_t kTrackKenSpecials1 = 0x8228E440;
  static constexpr uint32_t kTrackSpecials1 = 0x82265228;
  static constexpr uint32_t kApplyEvilRyuPalette1 = 0x82318EF0;
  static constexpr uint32_t kPlayerResults1 = 0x823C8468;
  static constexpr uint32_t kPlayerResultsCharacter1 = 0x823C85E0;
  static constexpr uint32_t kRecordMatch1 = 0x823C33C8;
  static constexpr uint32_t kResetPlayerResults1 = 0x823C8540;
  static constexpr uint32_t kShowQuote1 = 0x823CE430;
  static constexpr uint32_t kShowWinText1 = 0x823CDBF8;
  // DEBUG_SAULO Arcade final-boss shortcut: the per-frame game state dispatch, the ending choice, the ladder build and the
  // next-opponent choice, and the guest's "Bison only" and "Akuma only" start-code flags.
  static constexpr uint32_t kArcadeFrame1 = 0x8223DD58;
  static constexpr uint32_t kArcadeEnding1 = 0x8223A5A0;
  static constexpr uint32_t kArcadeLadderInit1 = 0x82380C98;
  static constexpr uint32_t kArcadeNextOpponent1 = 0x823804A0;
  static constexpr uint32_t kArcadeBossOnlyFlag = 0x83CA2906;
  static constexpr uint32_t kArcadeAkumaOnlyFlag = 0x83CA0BBF;
  static constexpr uint32_t kArcadeFrame1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B52DC, 0xA16B0000, 0x3D4083CD, 0x394A52CC};
  static constexpr uint32_t kArcadeEnding1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x38800000, 0x3D608200, 0x386B64C0, 0x48193641, 0x3D6083CD};
  static constexpr uint32_t kArcadeLadderInit1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD, 0x816B52DC};
  static constexpr uint32_t kArcadeNextOpponent1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B52A0, 0x3D6083CD, 0x816B52DC};
};

struct TitleUpdate023UltraHooks {
  static constexpr uint32_t kTrackHalfCircle1 = 0x822653E8;
  static constexpr uint32_t kUnlockAkuma1 = 0x823C9C60;
  static constexpr uint32_t kNavigateSelect1 = 0x82243DE0;
  static constexpr uint32_t kPlayerCharacterIndex1 = 0x823C9478;
  static constexpr uint32_t kAnnounceWinnerName1 = 0x82342048;
  static constexpr uint32_t kLoadSounds1 = 0x823D63D8;
  static constexpr uint32_t kRequestSound1 = 0x82375D30;
  static constexpr uint32_t kRoundEndScream1 = 0x8223CEF8;
  static constexpr uint32_t kObserveLock1 = 0x82246D80;
  static constexpr uint32_t kPlaceCursor1 = 0x8232DE58;
  static constexpr uint32_t kPlayEnding1 = 0x823CC460;
  static constexpr uint32_t kGateSuper1 = 0x8238D398;
  static constexpr uint32_t kDrawPortraits1 = 0x82179EF8;
  static constexpr uint32_t kDrawRoster1 = 0x82177748;
  static constexpr uint32_t kDrawSprite1 = 0x8217AC20;
  static constexpr uint32_t kUpdateHud1 = 0x823D0780;
  static constexpr uint32_t kEmitSprites1 = 0x823A0390;
  static constexpr uint32_t kEmitSprites2 = 0x823A0650;
  static constexpr uint32_t kEmitSprites3 = 0x823A2F48;
  static constexpr uint32_t kEmitSprites4 = 0x823A4850;
  static constexpr uint32_t kEmitSprites5 = 0x823A5DE8;
  static constexpr uint32_t kHeavyPunch1 = 0x8225E228;
  static constexpr uint32_t kLoadGrabRange1 = 0x821C1F90;
  static constexpr uint32_t kLookupAction1 = 0x821C0318;
  static constexpr uint32_t kRunKenSpecialClass1 = 0x8228ED58;
  static constexpr uint32_t kRunSpecialClass1 = 0x822654B0;
  static constexpr uint32_t kSelectCpuMove1 = 0x821953A0;
  static constexpr uint32_t kStartJump1 = 0x821B4318;
  static constexpr uint32_t kStartJump2 = 0x8218B998;
  static constexpr uint32_t kTrackKenKickCommand1 = 0x8228E160;
  static constexpr uint32_t kTrackKenKickCommand2 = 0x8228E1C8;
  static constexpr uint32_t kTrackKenKickCommand3 = 0x8228E230;
  static constexpr uint32_t kTrackKenSpecials1 = 0x8228ED10;
  static constexpr uint32_t kTrackSpecials1 = 0x82265698;
  static constexpr uint32_t kApplyEvilRyuPalette1 = 0x82318E30;
  static constexpr uint32_t kPlayerResults1 = 0x823C8A60;
  static constexpr uint32_t kPlayerResultsCharacter1 = 0x823C8BD8;
  static constexpr uint32_t kRecordMatch1 = 0x823C3970;
  static constexpr uint32_t kResetPlayerResults1 = 0x823C8B38;
  static constexpr uint32_t kShowQuote1 = 0x823CEA10;
  static constexpr uint32_t kShowWinText1 = 0x823CE1D8;
  static constexpr uint32_t kArcadeFrame1 = 0x8223DE80;
  static constexpr uint32_t kArcadeEnding1 = 0x8223A6C8;
  static constexpr uint32_t kArcadeLadderInit1 = 0x82380D68;
  static constexpr uint32_t kArcadeNextOpponent1 = 0x82380570;
  static constexpr uint32_t kArcadeBossOnlyFlag = 0x83CA2E1E;
  static constexpr uint32_t kArcadeAkumaOnlyFlag = 0x83CA10D7;
  static constexpr uint32_t kArcadeFrame1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x816B57F4, 0xA16B0000, 0x3D4083CD, 0x394A57E4};
  static constexpr uint32_t kArcadeEnding1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x38800000, 0x3D608200, 0x386B64C0, 0x48193AF9, 0x3D6083CD};
  static constexpr uint32_t kArcadeLadderInit1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FF90, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD, 0x816B57F4};
  static constexpr uint32_t kArcadeNextOpponent1Words[] = {0x7D8802A6, 0x9181FFF8, 0x9421FFA0, 0x3D6083CD, 0x39400000, 0x914B57B8, 0x3D6083CD, 0x816B57F4};
};

}  // namespace ssf2t::character
