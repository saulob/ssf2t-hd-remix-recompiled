// Game UI tests: the message box replacement (text decoding, results, the wrapper and the invite confirmation's call
// to it in both images) and the menus' input object the invite user comes from. Headless.
// Run: ssf2t_guest_ui_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "guest_memory.h"
#include "message_box.h"
#include "player_match_guest.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(expr)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(expr)) {                                                  \
      ++g_failures;                                                 \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                               \
  } while (0)

using namespace ssf2t;
namespace fs = std::filesystem;

constexpr uint32_t kImageBase = 0x82000000;

std::vector<uint8_t> Utf16Be(std::u16string_view text) {
  std::vector<uint8_t> out;
  for (const char16_t c : text) {
    out.push_back(uint8_t(c >> 8));
    out.push_back(uint8_t(c));
  }
  out.push_back(0);
  out.push_back(0);
  return out;
}

void TestGuestTextDecodes() {
  CHECK(Utf16BeToUtf8(Utf16Be(u"Accept invitation").data()) == "Accept invitation");
  CHECK(Utf16BeToUtf8(Utf16Be(u"café").data()) == "caf\xC3\xA9");
  CHECK(Utf16BeToUtf8(Utf16Be(u"ス").data()) == "\xE3\x82\xB9");
  CHECK(Utf16BeToUtf8(Utf16Be(u"\U0001F600").data()) == "\xF0\x9F\x98\x80");
  const std::vector<uint8_t> lone = {0xD8, 0x00, 0x00, 0x41, 0x00, 0x00};
  CHECK(Utf16BeToUtf8(lone.data()) == "\xEF\xBF\xBD" "A");
  CHECK(Utf16BeToUtf8(Utf16Be(u"abcdef").data(), 3) == "abc");
  CHECK(Utf16BeToUtf8(nullptr).empty());
}

// B or Escape answers like the console: ERROR_CANCELLED and no button, which the invite confirmation treats as
// "No, continue current session".
void TestResults() {
  const MessageBoxResult yes = ChosenButton(0);
  CHECK(yes.status == 0 && yes.button && *yes.button == 0);
  const MessageBoxResult no = ChosenButton(1);
  CHECK(no.status == 0 && no.button && *no.button == 1);
  const MessageBoxResult cancelled = CancelledBox();
  CHECK(cancelled.status == 1223 && !cancelled.button);
  CHECK(FocusedButton(1, 2) == 1);
  CHECK(FocusedButton(2, 2) == 0);
  CHECK(FocusedButton(0xFFFFFFFFu, 3) == 0);
  CHECK(MessageBoxShape(2, 0x83E38664, 0x83E3866C));
  CHECK(!MessageBoxShape(0, 0x83E38664, 0x83E3866C));
  CHECK(!MessageBoxShape(4, 0x83E38664, 0x83E3866C));
  CHECK(!MessageBoxShape(2, 0, 0x83E3866C));
  CHECK(!MessageBoxShape(2, 0x83E38664, 0));
}

bool ReadWords(const fs::path& image, uint32_t address, uint32_t* out, size_t count) {
  std::ifstream in(image, std::ios::binary);
  if (!in) return false;
  in.seekg(address - kImageBase);
  for (size_t i = 0; i < count; ++i) {
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) return false;
    out[i] = LoadBE32(b, 0);
  }
  return true;
}

uint32_t BranchTarget(uint32_t address, uint32_t word) {
  const int32_t offset = int32_t(word << 6) >> 6 & ~3;
  return address + uint32_t(offset);
}

struct ImageCase {
  const char* name;
  const char* file;
  uint32_t wrapper;
  uint32_t invite_box_call;
  uint32_t input_get_state;
  uint32_t ui_input_pointer;
  uint32_t ui_controller_used;
  uint32_t request_sign_in;
  uint32_t confirm_sign_in_return;
  uint32_t join_lobby_from_invite;
  uint32_t decline_invite;
  uint32_t net_manager;
};

// `ori rX,rX,0x27F8` with the 0x20000 already in rX: the accepted-invite word of the net manager.
bool TouchesAcceptedInvite(uint32_t word) {
  return (word & 0xFC00FFFF) == (0x60000000 | kAcceptedInviteOffset);
}

void CheckImage(const ImageCase& c) {
  const fs::path image = fs::path(SSF2T_SOURCE_DIR) / "game" / "analysis" / c.file;
  uint32_t wrapper[5];
  if (!ReadWords(image, c.wrapper, wrapper, 5)) {
    std::printf("  SKIP %s: %s not found\n", c.name, image.string().c_str());
    return;
  }
  uint8_t bytes[20];
  for (int i = 0; i < 5; ++i) StoreBE32(bytes, i * 4, wrapper[i]);
  CHECK(IsMessageBoxWrapper(bytes));
  // The invite confirmation (invite_box_title / invite_box_message, Yes / No) calls the wrapper.
  uint32_t call = 0;
  CHECK(ReadWords(image, c.invite_box_call, &call, 1));
  CHECK((call & 0xFC000003) == 0x48000001 && BranchTarget(c.invite_box_call, call) == c.wrapper);
  // XInputGetState(user, state) is `mr r5,r4; li r4,1; b XamInputGetState`.
  uint32_t poll[3];
  CHECK(ReadWords(image, c.input_get_state, poll, 3));
  CHECK(poll[0] == 0x7C852378 && poll[1] == 0x38800001 && (poll[2] & 0xFC000003) == 0x48000000);
  // GetUIControllerUsed loads the input object: lis r11,hi / lwz r3,lo(r11).
  uint32_t load[2];
  CHECK(ReadWords(image, c.ui_controller_used + 0x14, load, 2));
  CHECK((load[0] >> 26) == 15 && (load[1] >> 26) == 32);
  CHECK((load[0] << 16) + uint32_t(int32_t(int16_t(load[1] & 0xFFFF))) == c.ui_input_pointer);
  // The accept box polls `li r3,1; bl RequestOnlineSignIn` once a frame.
  uint32_t gate[2];
  CHECK(ReadWords(image, c.confirm_sign_in_return - 8, gate, 2));
  CHECK(gate[0] == 0x38600001 && (gate[1] & 0xFC000003) == 0x48000001 &&
        BranchTarget(c.confirm_sign_in_return - 4, gate[1]) == c.request_sign_in);
  // JoinLobbyFromInvite is `li r3,1; bl join`, and the join clears the accepted invite.
  uint32_t join[2];
  CHECK(ReadWords(image, c.join_lobby_from_invite + 0x20, join, 2));
  CHECK(join[0] == 0x38600001 && (join[1] & 0xFC000003) == 0x48000001);
  uint32_t clear = 0;
  CHECK(ReadWords(image, BranchTarget(c.join_lobby_from_invite + 0x24, join[1]) + 0x48, &clear, 1));
  CHECK(TouchesAcceptedInvite(clear));
  // The decline reads the same word: lis r11,hi; lis r10,2; addi r11,r11,lo; ori r10,r10,0x27F8.
  uint32_t decline[4];
  CHECK(ReadWords(image, c.decline_invite + 0x0C, decline, 4));
  CHECK((decline[0] >> 26) == 15 && decline[1] == 0x3D400002 && (decline[2] >> 26) == 14 &&
        TouchesAcceptedInvite(decline[3]));
  CHECK((decline[0] << 16) + uint32_t(int32_t(int16_t(decline[2] & 0xFFFF))) == c.net_manager);
}

void TestImages() {
  using B = Base013PlayerMatch;
  using T = TitleUpdate023PlayerMatch;
  CheckImage({"base013", "default-0.0.1.3.image.bin", Base013MessageBox::kShowMessageBoxWrapper, 0x823EC6F4,
              B::kInputGetState, B::kUiInputPointer, 0x823CB740, B::kRequestOnlineSignIn, B::kInviteConfirmSignInReturn,
              B::kJoinLobbyFromInvite, B::kDeclineAcceptedInvite, B::kNetManager});
  CheckImage({"tu023", "default.image.bin", TitleUpdate023MessageBox::kShowMessageBoxWrapper, 0x823ECDE4,
              T::kInputGetState, T::kUiInputPointer, 0x823CBD20, T::kRequestOnlineSignIn, T::kInviteConfirmSignInReturn,
              T::kJoinLobbyFromInvite, T::kDeclineAcceptedInvite, T::kNetManager});
  std::vector<uint8_t> other(20, 0);
  CHECK(!IsMessageBoxWrapper(other.data()));
}

}  // namespace

int main() {
  TestGuestTextDecodes();
  TestResults();
  TestImages();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
