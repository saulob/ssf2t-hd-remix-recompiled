// Online Match menu tests: the UIScreenSet edits (Multiplayer with Local Match and Online Match only, Online Match
// with Find Match and Create Match only, Create Player Match without private slots or the lobby size), the help-line
// table, and the hook wiring for both images.
// Pure logic and source reads, no window or game files.
// Run: ssf2t_online_menu_tests

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

#include "guest_memory.h"
#include "online_menu.h"
#include "player_match.h"
#include "player_match_guest.h"

using namespace ssf2t;

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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string ReadSource(const char* name) { return ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "src" / name); }

bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

size_t Count(const std::string& text, const std::string& needle) {
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++n;
  return n;
}

std::string Tabs(int n) { return std::string(n, '\t'); }

std::string Row(const std::string& name) {
  return Tabs(3) + "<row>\n" + Tabs(4) + "<name>" + name + "</name>\n" + Tabs(3) + "</row>\n";
}

std::string Text(const std::string& name, const std::string& pos, const std::string& event, const std::string& text_id,
                 bool hidden = false) {
  std::string s = Tabs(5) + "<UIText>\n" + Tabs(6) + "<name>" + name + "</name>\n" + Tabs(6) +
                  "<descName>default_text</descName>\n";
  if (hidden) s += Tabs(6) + "<visible>false</visible>\n";
  s += Tabs(6) + "<pos>" + pos + "</pos>\n" + Tabs(6) + "<animEvent>\n" + Tabs(7) +
       "<anim>turn_transparent_quick</anim>\n" + Tabs(7) + "<event>activate" + event + "</event>\n" + Tabs(6) +
       "</animEvent>\n" + Tabs(6) + "<textID>" + text_id + "</textID>\n" + Tabs(5) + "</UIText>\n";
  return s;
}

std::string Highlight(const std::string& anim, const std::string& event) {
  return Tabs(5) + "<animEvent>\n" + Tabs(6) + "<anim>" + anim + "</anim>\n" + Tabs(6) + "<event>activate" + event +
         "</event>\n" + Tabs(5) + "</animEvent>\n";
}

std::string Script(const std::string& script, const std::string& event) {
  return Tabs(5) + "<scriptEvent>\n" + Tabs(6) + "<script>" + script + "</script>\n" + Tabs(6) + "<event>" + event +
         "</event>\n" + Tabs(5) + "</scriptEvent>\n";
}

std::string Item(const std::string& tag, const std::string& name, const std::string& event,
                 const std::string& accept) {
  return Tabs(4) + "<" + tag + ">\n" + Tabs(5) + "<name>" + name + "</name>\n" +
         Script("GeneralScreenActivateItem", "yinc") + Script("GeneralScreenActivateItem", "ydec") +
         Script("UIEvent_SendToTopScreen(activate" + event + ");", "activate") +
         Script("UIEvent_SendToTopScreen(deactivate" + event + ");", "deactivate") +
         (accept.empty() ? "" : Script(accept, "accept")) + Tabs(4) + "</" + tag + ">\n";
}

std::string Accept(const char* signal) {
  return std::string("GameFlowSignal(") + signal + "); GetUIControllerUsed(); SetControllerMask(result);";
}

// The layout of the game's MultiplayerGame screen (and its PS3 twin, which must stay as authored).
std::string MultiplayerScreen(const std::string& screen) {
  std::string s = Tabs(1) + "<UIScreen>\n" + Tabs(2) + "<name>" + screen + "</name>\n" + Tabs(2) +
                  "<UIActivationGrid>\n" + Row("multiplayerItem1") + Row("multiplayerItem2") +
                  Row("multiplayerItem3") + Row("multiplayerItem4") + Tabs(2) + "</UIActivationGrid>\n" + Tabs(2) +
                  "<uiElements>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>activefonts</name>\n";
  s += Text("LocalMatch_hilight", "-180 363 10", "LocalMatch", "LOCAL_MATCH_L");
  s += Text("PlayerMatch_hilight", "-120 483 10", "PlayerMatch", "PLAYER_MATCH_L");
  s += Text("RankedMatch_hilight", "-150 423 10", "RankedMatch", "RANKED_MATCH_L");
  s += Text("TournamentMatch_hilight", "-90 543 10", "TournamentMatch", "TOURNAMENT_MATCH_L");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>highlight_move_group</name>\n";
  s += Highlight("Highlight_PositionAnim_L1", "LocalMatch") + Highlight("Highlight_PositionAnim_L2", "RankedMatch") +
       Highlight("Highlight_PositionAnim_L3", "PlayerMatch") +
       Highlight("Highlight_PositionAnim_L4", "TournamentMatch");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>inactivefonts</name>\n";
  s += Text("LocalMatch", "-180 363 16", "LocalMatch", "LOCAL_MATCH_L");
  s += Text("PlayerMatch", "-120 483 16", "PlayerMatch", "PLAYER_MATCH_L");
  s += Text("PlayerMatch_Trial", "-120 483 16", "PlayerMatch", "PLAYER_MATCH_L", true);
  s += Text("RankedMatch", "-150 423 16", "RankedMatch", "RANKED_MATCH_L");
  s += Text("RankedMatch_Trial", "-150 423 16", "RankedMatch", "RANKED_MATCH_L", true);
  s += Text("TournamentMatch", "-90 543 16", "TournamentMatch", "TOURNAMENT_MATCH_L");
  s += Text("TournamentMatch_Trial", "-90 543 16", "TournamentMatch", "TOURNAMENT_MATCH_L", true);
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(3) + "<UIElementGroup>\n" + Tabs(4) + "<name>temp_activate_items</name>\n";
  s += Item("UIGraphic", "multiplayerItem1", "localmatch", Accept("accept_VersusModeSelect"));
  s += Item("UIGraphic", "multiplayerItem2", "rankedmatch", Accept("accept_RankedMatch"));
  s += Item("UIGraphic", "multiplayerItem3", "playermatch", Accept("accept_PlayerMatch"));
  s += Item("UIGraphic", "multiplayerItem4", "TournamentMatch", Accept("accept_TournamentMatch"));
  s += Tabs(3) + "</UIElementGroup>\n" + Tabs(2) + "</uiElements>\n" + Tabs(1) + "</UIScreen>\n";
  return s;
}

// The layout of the game's PlayerMatch screen (Online Match) and its PS3 twin: Quick, Custom and Create Match.
std::string PlayerMatchScreen(const std::string& screen) {
  std::string s = Tabs(1) + "<UIScreen>\n" + Tabs(2) + "<name>" + screen + "</name>\n" + Tabs(2) +
                  "<UIActivationGrid>\n" + Row("playermatchItem1") + Row("playermatchItem2") +
                  Row("playermatchItem3") + Tabs(2) + "</UIActivationGrid>\n" + Tabs(2) + "<uiElements>\n" + Tabs(4) +
                  "<UIElementGroup>\n" + Tabs(5) + "<name>activefonts</name>\n";
  s += Text("creatematch_hilight", "-120 483 10", "CreateMatch", "CREATE_MATCH_L");
  s += Text("custommatch_hilight", "-150 423 10", "CustomMatch", "CUSTOM_MATCH_L");
  s += Text("quickmatch_hilight", "-180 363 10", "QuickMatch", "QUICK_MATCH_L");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>highlight_move_group</name>\n";
  s += Highlight("Highlight_PositionAnim_L1", "QuickMatch") + Highlight("Highlight_PositionAnim_L2", "CustomMatch") +
       Highlight("Highlight_PositionAnim_L3", "CreateMatch");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>inactivefonts</name>\n";
  s += Text("creatematch", "-120 483 16", "CreateMatch", "CREATE_MATCH_L");
  s += Text("custommatch", "-150 423 16", "CustomMatch", "CUSTOM_MATCH_L");
  s += Text("quickmatch", "-180 363 16", "QuickMatch", "QUICK_MATCH_L");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(3) + "<UIElementGroup>\n" + Tabs(4) + "<name>temp_activate_items</name>\n";
  s += Item("UIGraphic", "playermatchItem1", "quickmatch", "GameflowSignal(accept_QuickMatch);");
  s += Item("UIGraphic", "playermatchItem2", "custommatch", "GameflowSignal(accept_CustomPlayerMatch);");
  s += Item("UIGraphic", "playermatchItem3", "creatematch", "GameflowSignal(accept_CreatePlayerMatch);");
  s += Tabs(3) + "</UIElementGroup>\n" + Tabs(2) + "</uiElements>\n" + Tabs(1) + "</UIScreen>\n";
  return s;
}

std::string CreateMatchScreen() {
  const char* rows[] = {"gamemodes_values",    "timer_values",   "roundstowin_values",
                        "gamespeed_values",    "classicakuma_values", "handicap_values",
                        "numprivateslots_values", "numplayers_values", "voicechat_values"};
  std::string s = Tabs(1) + "<UIScreen>\n" + Tabs(2) + "<name>CreatePlayerMatch</name>\n" + Tabs(2) +
                  "<UIActivationGrid>\n";
  for (const char* row : rows) s += Row(row);
  s += Tabs(2) + "</UIActivationGrid>\n" + Tabs(2) + "<uiElements>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) +
       "<name>activefonts</name>\n";
  s += Text("PrivateSlots_hilight", "-80 640 11", "PrivateSlots", "PRIVATE_SLOTS_L");
  s += Text("PrivateSlotsvalue_hilight", "520 645 11", "PrivateSlots", "TEMP_VALUE");
  s += Text("NumberofPlayers_hilight", "-60 690 11", "NumberofPlayers", "NO_PLAYERS_L");
  s += Text("NumberofPlayersvalue_hilight", "540 695 11", "NumberofPlayers", "TEMP_VALUE");
  s += Text("VoiceChat_hilight", "-40 740 11", "VoiceChat", "VOICE_CHAT_L");
  s += Text("VoiceChatvalue_hilight", "560 745 11", "VoiceChat", "TEMP_VALUE");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>highlight_move_group</name>\n";
  for (int i = 1; i <= 6; ++i) s += Highlight("Highlight_CreateMatch_L" + std::to_string(i), "Row" + std::to_string(i));
  s += Highlight("Highlight_CreateMatch_L7", "PrivateSlots");
  s += Highlight("Highlight_CreateMatch_L8", "NumberofPlayers") + Highlight("Highlight_CreateMatch_L9", "VoiceChat");
  s += Tabs(4) + "</UIElementGroup>\n" + Tabs(4) + "<UIElementGroup>\n" + Tabs(5) + "<name>inactivefonts</name>\n";
  s += Text("PrivateSlots", "-80 640 14", "PrivateSlots", "PRIVATE_SLOTS_L");
  s += Text("PrivateSlotsvalue", "520 645 14", "PrivateSlots", "TEMP_VALUE");
  s += Text("NumberofPlayers", "-60 690 14", "NumberofPlayers", "NO_PLAYERS_L");
  s += Text("NumberofPlayersvalue", "540 695 14", "NumberofPlayers", "TEMP_VALUE");
  s += Text("VoiceChat", "-40 740 14", "VoiceChat", "VOICE_CHAT_L");
  s += Text("VoiceChatvalue", "560 745 14", "VoiceChat", "TEMP_VALUE");
  s += Tabs(4) + "</UIElementGroup>\n";
  s += Item("UIGrid", "numprivateslots_values", "PrivateSlots", "");
  s += Item("UIGrid", "numplayers_values", "NumberofPlayers", "");
  s += Item("UIGrid", "voicechat_values", "VoiceChat", "");
  s += Tabs(2) + "</uiElements>\n" + Tabs(1) + "</UIScreen>\n";
  return s;
}

std::string LocalLobbyScreen() {
  return Tabs(1) + "<UIScreen>\n" + Tabs(2) + "<name>LocalMatchLobby</name>\n" + Tabs(2) + "<UIActivationGrid>\n" +
         Row("multiplayerItem2") + Tabs(2) + "</UIActivationGrid>\n" + Tabs(1) + "</UIScreen>\n";
}

std::string ScreenSet() {
  return "<UIScreenSet>\n" + CreateMatchScreen() + LocalLobbyScreen() + MultiplayerScreen("MultiplayerGame") +
         MultiplayerScreen("MultiplayerGame_ps3") + PlayerMatchScreen("PlayerMatch") +
         PlayerMatchScreen("PlayerMatch_ps3") + "</UIScreenSet>\n";
}

// The screen itself, not an element of another screen with the same name (Multiplayer has a PlayerMatch text).
std::string ScreenOf(const std::string& xml, const std::string& name) {
  const std::string name_tag = "<name>" + name + "</name>";
  for (size_t at = xml.find(name_tag); at != std::string::npos; at = xml.find(name_tag, at + 1)) {
    const size_t begin = xml.rfind("<UIScreen>", at);
    if (begin == std::string::npos) continue;
    const std::string between = xml.substr(begin + 10, at - begin - 10);
    if (between.find_first_not_of(" \t\r\n") != std::string::npos) continue;
    return xml.substr(begin, xml.find("</UIScreen>", at) - begin);
  }
  return {};
}

std::vector<std::string> GridRows(const std::string& screen) {
  std::vector<std::string> rows;
  const std::string grid = screen.substr(0, screen.find("</UIActivationGrid>"));
  const std::regex row(R"(<row>\s*<name>(\w+)</name>\s*</row>)");
  for (auto it = std::sregex_iterator(grid.begin(), grid.end(), row); it != std::sregex_iterator(); ++it) {
    rows.push_back((*it)[1]);
  }
  return rows;
}

// The element whose <name> is `name`, from its opening tag to its closing tag.
std::string ElementOf(const std::string& screen, const std::string& tag, const std::string& name) {
  const std::string open = "<" + tag + ">", close = "</" + tag + ">", name_tag = "<name>" + name + "</name>";
  for (size_t at = screen.find(name_tag); at != std::string::npos; at = screen.find(name_tag, at + 1)) {
    const size_t begin = screen.rfind(open, at);
    if (begin == std::string::npos) continue;
    const std::string between = screen.substr(begin + open.size(), at - begin - open.size());
    if (between.find_first_not_of(" \t\r\n") != std::string::npos) continue;
    return screen.substr(begin, screen.find(close, at) + close.size() - begin);
  }
  return {};
}

std::string AcceptScript(const std::string& screen, const std::string& item) {
  const std::string element = ElementOf(screen, "UIGraphic", item);
  const size_t accept = element.find("<event>accept</event>");
  if (accept == std::string::npos) return {};
  const size_t script = element.rfind("<script>", accept);
  return element.substr(script + 8, element.find("</script>", script) - script - 8);
}

void TestMultiplayerShowsTwoItems() {
  const std::string original = ScreenSet();
  const auto patch = PatchScreenSet(original);
  CHECK(patch && patch->multiplayer && patch->create_match);
  if (!patch) return;
  const std::string before = ScreenOf(original, "MultiplayerGame");
  const std::string screen = ScreenOf(patch->xml, "MultiplayerGame");
  CHECK(GridRows(before).size() == 4);
  const std::vector<std::string> rows = GridRows(screen);
  CHECK((rows == std::vector<std::string>{"multiplayerItem1", "multiplayerItem3"}));
  // Item 0 is Local Match, item 1 Online Match: each row still sends its original game-flow signal.
  CHECK(AcceptScript(screen, rows[0]) == Accept("accept_VersusModeSelect"));
  CHECK(AcceptScript(screen, rows[1]) == Accept("accept_PlayerMatch"));
  // Local Match is untouched.
  for (const char* name : {"LocalMatch_hilight", "LocalMatch"}) {
    CHECK(!ElementOf(screen, "UIText", name).empty() &&
          ElementOf(screen, "UIText", name) == ElementOf(before, "UIText", name));
  }
  CHECK(ElementOf(screen, "UIGraphic", "multiplayerItem1") == ElementOf(before, "UIGraphic", "multiplayerItem1"));
  CHECK(ElementOf(screen, "UIGraphic", "multiplayerItem3") == ElementOf(before, "UIGraphic", "multiplayerItem3"));
  // Online Match sits where the second row was, and the highlight follows it there.
  for (const char* name : {"PlayerMatch_hilight", "PlayerMatch", "PlayerMatch_Trial"}) {
    const std::string text = ElementOf(screen, "UIText", name);
    CHECK(Has(text, "<pos>-150 423 ") && !Has(text, "483"));
    CHECK(Has(text, "<textID>PLAYER_MATCH_L</textID>"));
  }
  CHECK(Has(screen, "<anim>Highlight_PositionAnim_L1</anim>\n\t\t\t\t\t\t<event>activateLocalMatch</event>"));
  CHECK(Has(screen, "<anim>Highlight_PositionAnim_L2</anim>\n\t\t\t\t\t\t<event>activatePlayerMatch</event>"));
  CHECK(!Has(screen, "Highlight_PositionAnim_L3") && !Has(screen, "Highlight_PositionAnim_L4"));
}

void TestRankedAndTournamentAreGone() {
  const auto patch = PatchScreenSet(ScreenSet());
  if (!patch) {
    CHECK(false);
    return;
  }
  const std::string screen = ScreenOf(patch->xml, "MultiplayerGame");
  for (const char* word : {"Ranked", "ranked", "Tournament", "multiplayerItem2", "multiplayerItem4"}) {
    CHECK(!Has(screen, word));
  }
  // No focus target without a row, and no row without its item: no phantom entries.
  CHECK(Count(screen, "<UIGraphic>") == GridRows(screen).size());
  for (const std::string& row : GridRows(screen)) CHECK(!ElementOf(screen, "UIGraphic", row).empty());
  CHECK(Count(screen, "<visible>false</visible>") == 1);  // PlayerMatch_Trial, as authored
}

// Find Match is the game's Quick Match row, renamed by the menu text table.
void TestOnlineMatchShowsFindAndCreate() {
  const std::string original = ScreenSet();
  const auto patch = PatchScreenSet(original);
  CHECK(patch && patch->player_match);
  if (!patch) return;
  const std::string before = ScreenOf(original, "PlayerMatch");
  const std::string screen = ScreenOf(patch->xml, "PlayerMatch");
  CHECK(GridRows(before).size() == 3);
  const std::vector<std::string> rows = GridRows(screen);
  CHECK((rows == std::vector<std::string>{"playermatchItem1", "playermatchItem3"}));
  if (rows.size() != 2) return;
  // Up and Down move between these two rows only; each keeps its own game-flow signal.
  CHECK(AcceptScript(screen, rows[0]) == "GameflowSignal(accept_QuickMatch);");
  CHECK(AcceptScript(screen, rows[1]) == "GameflowSignal(accept_CreatePlayerMatch);");
  for (const char* name : {"quickmatch_hilight", "quickmatch"}) {
    CHECK(!ElementOf(screen, "UIText", name).empty() &&
          ElementOf(screen, "UIText", name) == ElementOf(before, "UIText", name));
  }
  CHECK(ElementOf(screen, "UIGraphic", "playermatchItem1") == ElementOf(before, "UIGraphic", "playermatchItem1"));
  CHECK(ElementOf(screen, "UIGraphic", "playermatchItem3") == ElementOf(before, "UIGraphic", "playermatchItem3"));
  // Create Match sits where the second row was, and the highlight follows it there.
  for (const char* name : {"creatematch_hilight", "creatematch"}) {
    const std::string text = ElementOf(screen, "UIText", name);
    CHECK(Has(text, "<pos>-150 423 ") && !Has(text, "483"));
    CHECK(Has(text, "<textID>CREATE_MATCH_L</textID>"));
  }
  CHECK(Has(screen, "<anim>Highlight_PositionAnim_L1</anim>\n\t\t\t\t\t\t<event>activateQuickMatch</event>"));
  CHECK(Has(screen, "<anim>Highlight_PositionAnim_L2</anim>\n\t\t\t\t\t\t<event>activateCreateMatch</event>"));
  CHECK(!Has(screen, "Highlight_PositionAnim_L3"));
}

void TestCustomMatchIsGone() {
  const std::string original = ScreenSet();
  const auto patch = PatchScreenSet(original);
  if (!patch) {
    CHECK(false);
    return;
  }
  const std::string screen = ScreenOf(patch->xml, "PlayerMatch");
  for (const char* word : {"Custom", "custom", "CUSTOM", "playermatchItem2"}) CHECK(!Has(screen, word));
  // No invisible third row and no action without a row.
  CHECK(Count(screen, "<UIGraphic>") == GridRows(screen).size());
  for (const std::string& row : GridRows(screen)) CHECK(!ElementOf(screen, "UIGraphic", row).empty());
  CHECK(Count(screen, "GameflowSignal(") == 2);
  CHECK(ScreenOf(patch->xml, "PlayerMatch_ps3") == ScreenOf(original, "PlayerMatch_ps3"));
}

void TestOtherScreensStayAuthored() {
  const std::string original = ScreenSet();
  const auto patch = PatchScreenSet(original);
  if (!patch) {
    CHECK(false);
    return;
  }
  CHECK(ScreenOf(patch->xml, "MultiplayerGame_ps3") == ScreenOf(original, "MultiplayerGame_ps3"));
  CHECK(ScreenOf(patch->xml, "LocalMatchLobby") == ScreenOf(original, "LocalMatchLobby"));
  CHECK(ScreenOf(patch->xml, "PlayerMatch_ps3") == ScreenOf(original, "PlayerMatch_ps3"));
  CHECK(patch->xml.rfind("<UIScreenSet>", 0) == 0 && Has(patch->xml, "</UIScreenSet>"));
}

void TestCreateMatchHasNoLobbySize() {
  const std::string original = ScreenSet();
  const auto patch = PatchScreenSet(original);
  if (!patch) {
    CHECK(false);
    return;
  }
  const std::string screen = ScreenOf(patch->xml, "CreatePlayerMatch");
  const std::vector<std::string> rows = GridRows(screen);
  CHECK(rows.size() == 7);
  CHECK(!rows.empty() && rows.back() == "voicechat_values");
  // Two players in two public slots: no control for private slots or the lobby size remains.
  CHECK(!Has(screen, "numplayers_values") && !Has(screen, "numprivateslots_values"));
  CHECK(Count(screen, "<UIGrid>") == 1);
  // The update script still writes those values, so their texts are hidden rather than removed.
  for (const char* name : {"PrivateSlots_hilight", "PrivateSlotsvalue_hilight", "PrivateSlots", "PrivateSlotsvalue",
                           "NumberofPlayers_hilight", "NumberofPlayersvalue_hilight", "NumberofPlayers",
                           "NumberofPlayersvalue"}) {
    const std::string text = ElementOf(screen, "UIText", name);
    CHECK(Has(text, "</descName>\n\t\t\t\t\t\t<visible>false</visible>\n"));
  }
  // Voice Chat moves up into the first freed row, with the highlight.
  CHECK(Has(ElementOf(screen, "UIText", "VoiceChat"), "<pos>-80 640 14</pos>"));
  CHECK(Has(ElementOf(screen, "UIText", "VoiceChat_hilight"), "<pos>-80 640 11</pos>"));
  CHECK(Has(ElementOf(screen, "UIText", "VoiceChatvalue"), "<pos>520 645 14</pos>"));
  CHECK(Has(ElementOf(screen, "UIText", "VoiceChatvalue_hilight"), "<pos>520 645 11</pos>"));
  CHECK(Has(screen, "<anim>Highlight_CreateMatch_L7</anim>\n\t\t\t\t\t\t<event>activateVoiceChat</event>"));
  CHECK(!Has(screen, "Highlight_CreateMatch_L8") && !Has(screen, "Highlight_CreateMatch_L9"));
  CHECK(!Has(screen, "<event>activatePrivateSlots</event>\n\t\t\t\t\t</animEvent>"));
}

void TestPatchIsAllOrNothingPerScreen() {
  CHECK(!PatchScreenSet("<scripts><script name=\"x\"/></scripts>"));
  CHECK(!PatchScreenSet(""));
  const auto once = PatchScreenSet(ScreenSet());
  CHECK(once && !PatchScreenSet(once->xml));

  // A Multiplayer screen laid out differently is left whole; Create Player Match is still patched.
  std::string other = ScreenSet();
  const size_t trial = other.find("<name>RankedMatch_Trial</name>");
  other.replace(trial, 30, "<name>RankedMatch_Demo_</name>");
  const auto patch = PatchScreenSet(other);
  CHECK(patch && !patch->multiplayer && patch->player_match && patch->create_match);
  if (patch) CHECK(ScreenOf(patch->xml, "MultiplayerGame") == ScreenOf(other, "MultiplayerGame"));

  // A duplicated element makes the target ambiguous.
  std::string twice = ScreenSet();
  const std::string row = Row("multiplayerItem2");
  twice.insert(twice.find(row, twice.find("<name>MultiplayerGame</name>")), row);
  const auto ambiguous = PatchScreenSet(twice);
  CHECK(ambiguous && !ambiguous->multiplayer);

  // CRLF documents keep their line endings.
  std::string crlf;
  for (const char c : ScreenSet()) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
  const auto windows = PatchScreenSet(crlf);
  CHECK(windows && windows->multiplayer && windows->player_match && windows->create_match);
  if (windows) {
    CHECK(!Has(std::regex_replace(windows->xml, std::regex("\r\n"), ""), "\n"));
    CHECK(Has(windows->xml, "</descName>\r\n\t\t\t\t\t\t<visible>false</visible>\r\n"));
  }
}

std::vector<uint8_t> HelpTable() {
  std::vector<uint8_t> table(kHelpTextEntries * kHelpTextEntryBytes, 0);
  const auto put = [&](uint32_t index, const char* screen, std::vector<uint32_t> lines) {
    uint8_t* entry = table.data() + index * kHelpTextEntryBytes;
    StoreBE32(entry, 0, ScreenNameHash(screen));
    StoreBE32(entry, 4, uint32_t(lines.size()));
    StoreBE32(entry, 8, 0x8000 + index);
    for (uint32_t i = 0; i < lines.size(); ++i) StoreBE32(entry, 12 + 4 * i, lines[i]);
  };
  put(0, "arcadeModeSelect", {0x11, 0x12});
  put(5, "createPlayerMatch", {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58});
  put(9, "multiplayerGame", {0x90, 0x91, 0x92, 0x93});
  put(10, "multiplayerGame_ps3", {0xA0, 0xA1, 0xA2, 0xA3});
  put(12, "playerMatch", {0xC0, 0xC1, 0xC2});
  put(13, "playerMatch_ps3", {0xD0, 0xD1, 0xD2, 0xD3});
  return table;
}

std::vector<uint32_t> HelpLines(const std::vector<uint8_t>& table, uint32_t index) {
  const uint8_t* entry = table.data() + index * kHelpTextEntryBytes;
  std::vector<uint32_t> lines;
  for (uint32_t i = 0; i < LoadBE32(entry, 4); ++i) lines.push_back(LoadBE32(entry, 12 + 4 * i));
  return lines;
}

void TestHelpLinesFollowTheRows() {
  CHECK(ScreenNameHash("MULTIPLAYERGAME") == 0x0CB5DD33u);
  CHECK(ScreenNameHash("multiplayerGame") == ScreenNameHash(kMultiplayerScreen));
  CHECK(ScreenNameHash("createPlayerMatch") == 0xAF13AA95u);
  std::vector<uint8_t> table = HelpTable();
  const HelpTextPatch patch = PatchHelpText(table.data());
  CHECK(patch.multiplayer && patch.player_match && patch.create_match);
  CHECK((HelpLines(table, 9) == std::vector<uint32_t>{0x90, 0x92}));
  CHECK((HelpLines(table, 12) == std::vector<uint32_t>{0xC0, 0xC2}));
  CHECK((HelpLines(table, 13) == std::vector<uint32_t>{0xD0, 0xD1, 0xD2, 0xD3}));
  CHECK(LoadBE32(table.data() + 9 * kHelpTextEntryBytes, 12 + 4 * 2) == 0);
  CHECK((HelpLines(table, 5) == std::vector<uint32_t>{0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x58}));
  CHECK((HelpLines(table, 10) == std::vector<uint32_t>{0xA0, 0xA1, 0xA2, 0xA3}));
  CHECK((HelpLines(table, 0) == std::vector<uint32_t>{0x11, 0x12}));
  CHECK(HelpTextScreenText(table.data(), kMultiplayerScreen) == 0x8009);
  CHECK(HelpTextScreenText(table.data(), "Unknown") == 0);
  // A table that is already patched (or laid out differently) is left alone.
  const std::vector<uint8_t> once = table;
  const HelpTextPatch again = PatchHelpText(table.data());
  CHECK(!again.multiplayer && !again.player_match && !again.create_match && table == once);
}

void TestHooksCoverBothImages() {
  const std::string menu = ReadSource("online_menu_hook.cpp");
  for (const char* hook : {"base013_sub_82416E80", "base013_sub_823D4150", "base013_sub_823D44F8",
                           "tu023_sub_82417548", "tu023_sub_823D4820", "tu023_sub_823D4BC8"}) {
    CHECK(Has(menu, std::string("REX_HOOK_RAW(") + hook + ")") && Has(menu, std::string("REX_EXTERN(__imp__") + hook));
  }
  const size_t guard = menu.find("#if SSF2T_TITLE_UPDATE_IMAGE");
  CHECK(guard != std::string::npos && guard < menu.find("REX_HOOK_RAW(tu023_") && menu.find("base013_") < guard);
  CHECK(Base013PlayerMatch::kXmlDocumentParse == 0x82416E80 && TitleUpdate023PlayerMatch::kXmlDocumentParse == 0x82417548);
  CHECK(Base013PlayerMatch::kHelpTextLoad == 0x823D4150 && TitleUpdate023PlayerMatch::kHelpTextLoad == 0x823D4820);
  CHECK(Base013PlayerMatch::kHelpTextTable == 0x83C28140 && TitleUpdate023PlayerMatch::kHelpTextTable == 0x83C284B8);
  CHECK(Base013PlayerMatch::kErrorLevel == 0x83E37F74 && TitleUpdate023PlayerMatch::kErrorLevel == 0x83E3848C);
  CHECK(Has(menu, "SystemHeapFree(copy)"));
  const std::string cmake = ReadFile(std::filesystem::path(SSF2T_SOURCE_DIR) / "CMakeLists.txt");
  CHECK(Has(cmake, "src/online_menu.cpp") && Has(cmake, "src/online_menu_hook.cpp"));
}

void TestGateHookUsesTheProvenConvention() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  const size_t begin = hook.find("static void RequestOnlineSignIn(");
  const std::string gate = hook.substr(begin, hook.find("static void SetError(", begin) - begin);
  // Authenticated: the guest's own gate runs and decides; its result is never forced.
  CHECK(Has(gate, "original(ctx, base);"));
  CHECK(!Has(gate, "ctx.r3.u64 = 1"));
  CHECK(Has(gate, "ssf2t::SignInAllowed(ctx.r3.u32) ? \"allow\" : \"deny\""));
  // Online play on, service down: ONLINE UNAVAILABLE instead of the Xbox LIVE wording.
  CHECK(Has(gate, "ssf2t::OnlineGate::kServiceUnavailable"));
  CHECK(Has(gate, "ssf2t::kOnlineUnavailableTitle") && Has(gate, "ctx.r3.u64 = 0;"));
  // The LIVE profile is the active user's, not always user 0.
  CHECK(Has(hook, "user == LiveUser<G>(base)"));
  CHECK(!Has(hook, "user == 0 &&") && !Has(hook, "(user == 0 ||"));
  CHECK(!Has(hook, "NotAvailable") && !Has(hook, "82442210") && !Has(hook, "82442940"));
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_823EAEE8) { Base::SetError("));
  CHECK(Has(hook, "REX_HOOK_RAW(tu023_sub_823EB5D8) { Update::SetError("));
  // Create Player Match is held at two players on every path.
  CHECK(Count(hook, "ForcePlayerMatchSlots<G>(base);") == 3);
  const size_t lobby = hook.find("static void CreateLobby(");
  CHECK(lobby != std::string::npos &&
        hook.find("ForcePlayerMatchSlots<G>(base);", lobby) < hook.find("original(ctx, base);", lobby));
  CHECK(Has(hook, "StoreBE32(base, private_at, 0);"));
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_823BD588) { Base::CreateLobby(") &&
        Has(hook, "REX_HOOK_RAW(tu023_sub_823BD540) { Update::CreateLobby("));
  const std::string net = ReadSource("player_match_net_hook.cpp");
  CHECK(Has(net, "user == ssf2t::LiveUserIndex(LoadBE32(base, G::kUserIndex))"));
}

void TestTelemetryNamesTheGate() {
  const std::string hook = ReadSource("player_match_hook.cpp");
  const std::string menu = ReadSource("online_menu_hook.cpp");
  for (const char* line : {"DEBUG_SAULO online_match selected", "DEBUG_SAULO online_match gate begin",
                           "DEBUG_SAULO online sign-in request begin argument=",
                           "DEBUG_SAULO online sign-in request original_result=",
                           "DEBUG_SAULO online sign-in request override_result=",
                           "DEBUG_SAULO online sign-in request caller=", "DEBUG_SAULO online sign-in request branch=",
                           "DEBUG_SAULO online sign-in request popup=", "DEBUG_SAULO connection_required popup source=",
                           "DEBUG_SAULO online_match enter player_match_fsm"}) {
    CHECK(Has(hook, line));
  }
  CHECK(Has(menu, "DEBUG_SAULO multiplayer menu enter"));
  CHECK(Has(menu, "DEBUG_SAULO online_match menu items find=1 custom={} create=1"));
  CHECK(Has(menu, "DEBUG_SAULO multiplayer menu items local=1 online=1 ranked={} tournament={}"));
  CHECK(Has(menu, "if (in_multiplayer && !g_in_multiplayer)"));
}

// The Game Lobby's scripts as the game ships them (unrelated statements trimmed), with the Leaderboard's own gamer card
// lookup that must stay.
std::string LobbyScripts(bool gamer_card_call = true) {
  return "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<scripts>\n"
         "\t<script name=\"GameLobbyUpdateHelpText\">\n"
         "      if( IsSpectator() )\n      {        \n"
         "        UIText_SetString_TextID(screenHelpText, tid_GameLobbyHelp_spectator);\n      }\n"
         "      else\n      {\n        UIText_SetString_TextID(screenHelpText, tid_GameLobbyHelp);\n      }\n"
         "      if( IsPS3() )\n      {\n        UIText_SetString_TextID(selectionHelpText, tid_blank);\n      }\n"
         "      else\n      {\n        UIText_SetString_TextID(selectionHelpText, tid_glGamertagSlot01);\n      }\n"
         "\t</script>\n"
         "\t<script name=\"GameLobbyUpdate\">\n"
         "\t    if (IsNewlyPressedPF(BUTTON3, 0))\n\t    {\n"
         "\t\t    /* X button. Invite only for player/friendly matches. */\n"
         "\t\t    if ( GameFlowIsOnStack(player_match_fsm) )\n\t\t    {\n"
         "          GeneralScreenAccept();\n  \t      InviteToGame();  \n\t\t    }\n\t    }\n"
         "\t    else if (IsNewlyPressedPF(BUTTON4, 0))\n\t    {\n"
         "\t\t    /* Y button */\n        /* DO NOT ALLOW LOOKUP IN RANKED MATCH */\n"
         "        if( GetGameType() ne 1 )\n        {\n          if( not IsPS3() )\n          {\n"
         "            GeneralScreenAccept();\n\t\t        UIActivationGrid_GetCurrentIndex();\n" +
         std::string(gamer_card_call ? "\t\t        ViewGamercard(result);\n" : "\t\t        PlayerReview(result);\n") +
         "          }\n        }\n\t    }\n"
         "\t    else if (IsNewlyPressedPF(BUTTON2, 0))\n\t    {\n"
         "\t\t    /* B button */\n\t\t    GameFlowSignal(go_back);\n\t    }     \n"
         "\t    else if (IsNewlyPressedPF(BUTTON1, 0))\n\t    {\n"
         "\t\t    /* A button */\n        if( not IsSpectator() )\n        {\n"
         "\t\t      ToggleReady();\n          GeneralScreenAccept();\n        }\n\t    }     \n"
         "\t    else if (IsNewlyPressedPF(LEFT_TRIGGER, 0))\n\t    {\n"
         "\t\t    /* LB button */\n        if( not IsPS3() )\n        {\n"
         "          GeneralScreenAccept();\n\t\t      ShowPlayersUI();\n        }\n\t    }\n"
         "      /* Feature Removed\n      else if (IsNewlyPressedPF(RIGHT_TRIGGER, 0))\n\t    {\n"
         "        GeneralScreenAccept();\n      } \n      */\n"
         "\t    GameLobbySetDefaultState();\n"
         "\t</script>\n"
         "\t<script name=\"LeaderboardUpdate\">\n"
         "\t    if (IsNewlyPressedPF(BUTTON4, 0))\n\t    {\n\t\t    ViewGamercard(result);\n\t    }\n"
         "\t</script>\n</scripts>\n";
}

std::string ScriptBody(const std::string& xml, const std::string& name) {
  const size_t begin = xml.find("<script name=\"" + name + "\">");
  return begin == std::string::npos ? std::string() : xml.substr(begin, xml.find("</script>", begin) - begin);
}

// The Guide's gamer card and players list are ReXGlue stubs: their buttons go, Invite Player, Start Match (ready) and
// Leave Lobby stay.
void TestGameLobbyKeepsOnlyWorkingActions() {
  const auto patch = PatchLobbyScripts(LobbyScripts());
  CHECK(patch && patch->gamer_card && patch->players_list && patch->slot_help);
  if (!patch) return;
  const std::string update = ScriptBody(patch->xml, "GameLobbyUpdate");
  CHECK(!Has(update, "ViewGamercard(") && !Has(update, "BUTTON4"));
  CHECK(!Has(update, "ShowPlayersUI(") && !Has(update, "LEFT_TRIGGER"));
  CHECK(Has(update, "if (IsNewlyPressedPF(BUTTON3, 0))") && Has(update, "InviteToGame();"));
  CHECK(Has(update, "else if (IsNewlyPressedPF(BUTTON1, 0))") && Has(update, "ToggleReady();"));
  CHECK(Has(update, "else if (IsNewlyPressedPF(BUTTON2, 0))") && Has(update, "GameFlowSignal(go_back);"));
  CHECK(Has(update, "/* Feature Removed") && Has(update, "GameLobbySetDefaultState();"));
  CHECK(Count(update, "{") == Count(update, "}"));
  const std::string help = ScriptBody(patch->xml, "GameLobbyUpdateHelpText");
  CHECK(!Has(help, "tid_glGamertagSlot01") && Count(help, "UIText_SetString_TextID(selectionHelpText, tid_blank);") == 2);
  CHECK(Has(help, "UIText_SetString_TextID(screenHelpText, tid_GameLobbyHelp);"));
  // Other screens keep their own gamer card.
  CHECK(Has(ScriptBody(patch->xml, "LeaderboardUpdate"), "ViewGamercard(result);"));
  CHECK(!PatchLobbyScripts(patch->xml));
}

void TestGameLobbyEditsApplyOnlyToTheShippedLayout() {
  const auto other = PatchLobbyScripts(LobbyScripts(false));
  CHECK(other && !other->gamer_card && other->players_list && other->slot_help);
  if (other) CHECK(Has(ScriptBody(other->xml, "GameLobbyUpdate"), "else if (IsNewlyPressedPF(BUTTON4, 0))"));
  CHECK(!PatchLobbyScripts("<?xml version=\"1.0\"?>\n<scripts>\n\t<script name=\"Other\">\n\t</script>\n</scripts>\n"));
  CHECK(!PatchScreenSet(LobbyScripts()));
}

void TestGameLobbyScriptsHookAndTelemetry() {
  const std::string menu = ReadSource("online_menu_hook.cpp");
  CHECK(Has(menu, "StartsLikeScripts(guest_text)") && Has(menu, "ssf2t::PatchLobbyScripts(guest_text)"));
  CHECK(Has(menu, "DEBUG_SAULO game_lobby actions removed gamer_card={} players_list={} slot_help={}"));
  CHECK(Has(menu, "DEBUG_SAULO post_fight game_lobby {}"));
  CHECK(Has(menu, "ssf2t::kGameLobbyScreen"));
  CHECK(ScreenNameHash(kGameLobbyScreen) == ScreenNameHash("GAMELOBBY"));
}

}  // namespace

int main() {
  TestMultiplayerShowsTwoItems();
  TestRankedAndTournamentAreGone();
  TestOnlineMatchShowsFindAndCreate();
  TestCustomMatchIsGone();
  TestOtherScreensStayAuthored();
  TestCreateMatchHasNoLobbySize();
  TestPatchIsAllOrNothingPerScreen();
  TestHelpLinesFollowTheRows();
  TestHooksCoverBothImages();
  TestGateHookUsesTheProvenConvention();
  TestTelemetryNamesTheGate();
  TestGameLobbyKeepsOnlyWorkingActions();
  TestGameLobbyEditsApplyOnlyToTheShippedLayout();
  TestGameLobbyScriptsHookAndTelemetry();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
