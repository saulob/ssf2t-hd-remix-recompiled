#include "online_menu.h"

#include <cctype>

#include "guest_memory.h"

namespace ssf2t {
namespace {

constexpr size_t npos = std::string::npos;

struct Span {
  size_t begin = 0;
  size_t end = 0;
};

bool Blank(std::string_view text) { return text.find_first_not_of(" \t\r\n") == std::string_view::npos; }

std::string Open(std::string_view tag) { return "<" + std::string(tag) + ">"; }
std::string Close(std::string_view tag) { return "</" + std::string(tag) + ">"; }
std::string NameTag(std::string_view name) { return "<name>" + std::string(name) + "</name>"; }

// The one `tag` element whose first child is <name>`name`</name> and that holds no other `tag`.
std::optional<Span> Element(std::string_view text, std::string_view tag, std::string_view name) {
  const std::string open = Open(tag), close = Close(tag), name_tag = NameTag(name);
  std::optional<Span> found;
  for (size_t at = text.find(name_tag); at != npos; at = text.find(name_tag, at + 1)) {
    const size_t begin = text.rfind(open, at);
    if (begin == npos || !Blank(text.substr(begin + open.size(), at - begin - open.size()))) continue;
    const size_t end = text.find(close, at);
    if (end == npos || text.find(open, at) < end || found) return std::nullopt;
    found = Span{begin, end + close.size()};
  }
  return found;
}

size_t LineStart(const std::string& text, size_t at) {
  while (at > 0 && (text[at - 1] == ' ' || text[at - 1] == '\t')) --at;
  return at;
}

void EraseLines(std::string& text, Span span) {
  const size_t begin = LineStart(text, span.begin);
  size_t end = span.end;
  while (end < text.size() && (text[end] == ' ' || text[end] == '\t' || text[end] == '\r')) ++end;
  if (end < text.size() && text[end] == '\n') ++end;
  text.erase(begin, end - begin);
}

bool Erase(std::string& text, std::string_view tag, std::string_view name) {
  const std::optional<Span> span = Element(text, tag, name);
  if (!span) return false;
  EraseLines(text, *span);
  return true;
}

// Kept rather than erased where a screen script still writes to the element.
bool Hide(std::string& text, std::string_view tag, std::string_view name) {
  const std::optional<Span> span = Element(text, tag, name);
  if (!span) return false;
  const std::string_view element = std::string_view(text).substr(span->begin, span->end - span->begin);
  if (element.find("<visible>") != npos) return false;
  const size_t name_at = text.find(NameTag(name), span->begin);
  size_t after = name_at + NameTag(name).size();
  if (const size_t desc = element.find("</descName>"); desc != npos) after = span->begin + desc + 11;
  const std::string indent = text.substr(LineStart(text, name_at), name_at - LineStart(text, name_at));
  const std::string newline = text.find("\r\n") != npos ? "\r\n" : "\n";
  text.insert(after, newline + indent + "<visible>false</visible>");
  return true;
}

bool ReplaceIn(std::string& text, Span span, std::string_view from, std::string_view to) {
  const size_t at = text.find(from, span.begin);
  if (at == npos || at + from.size() > span.end) return false;
  if (const size_t again = text.find(from, at + 1); again != npos && again + from.size() <= span.end) return false;
  text.replace(at, from.size(), to);
  return true;
}

bool Move(std::string& text, std::string_view tag, std::string_view name, std::string_view from, std::string_view to) {
  const std::optional<Span> span = Element(text, tag, name);
  return span && ReplaceIn(text, *span, from, to);
}

bool ReplaceOnce(std::string& text, std::string_view from, std::string_view to) {
  return ReplaceIn(text, Span{0, text.size()}, from, to);
}

bool EraseAnimEvent(std::string& text, std::string_view anim) {
  const std::string anim_tag = "<anim>" + std::string(anim) + "</anim>";
  const size_t at = text.find(anim_tag);
  if (at == npos || text.find(anim_tag, at + 1) != npos) return false;
  const size_t begin = text.rfind("<animEvent>", at);
  const size_t end = text.find("</animEvent>", at);
  if (begin == npos || end == npos || text.find("</animEvent>", begin) < at) return false;
  EraseLines(text, Span{begin, end + std::string_view("</animEvent>").size()});
  return true;
}

// Rows: Local, Ranked, Player, Tournament. The remaining Player Match row takes the second row's place.
bool PatchMultiplayer(std::string& s) {
  constexpr const char* kRemovedTexts[] = {"RankedMatch_hilight", "TournamentMatch_hilight", "RankedMatch",
                                           "RankedMatch_Trial",   "TournamentMatch",         "TournamentMatch_Trial"};
  constexpr const char* kOnlineTexts[] = {"PlayerMatch_hilight", "PlayerMatch", "PlayerMatch_Trial"};
  bool ok = Erase(s, "row", "multiplayerItem2") && Erase(s, "row", "multiplayerItem4") &&
            Erase(s, "UIGraphic", "multiplayerItem2") && Erase(s, "UIGraphic", "multiplayerItem4");
  for (const char* name : kRemovedTexts) ok = ok && Erase(s, "UIText", name);
  ok = ok && EraseAnimEvent(s, "Highlight_PositionAnim_L2") && EraseAnimEvent(s, "Highlight_PositionAnim_L4") &&
       ReplaceOnce(s, "<anim>Highlight_PositionAnim_L3</anim>", "<anim>Highlight_PositionAnim_L2</anim>");
  for (const char* name : kOnlineTexts) ok = ok && Move(s, "UIText", name, "<pos>-120 483 ", "<pos>-150 423 ");
  return ok;
}

// Rows: Quick, Custom, Create. Custom Match's lobby list adds nothing to Quick Match and invites in a one against one
// match; Create Match takes the second row's place.
bool PatchPlayerMatch(std::string& s) {
  bool ok = Erase(s, "row", "playermatchItem2") && Erase(s, "UIGraphic", "playermatchItem2");
  for (const char* name : {"custommatch_hilight", "custommatch"}) ok = ok && Erase(s, "UIText", name);
  ok = ok && EraseAnimEvent(s, "Highlight_PositionAnim_L2") &&
       ReplaceOnce(s, "<anim>Highlight_PositionAnim_L3</anim>", "<anim>Highlight_PositionAnim_L2</anim>");
  for (const char* name : {"creatematch_hilight", "creatematch"}) {
    ok = ok && Move(s, "UIText", name, "<pos>-120 483 ", "<pos>-150 423 ");
  }
  return ok;
}

// Rows 6 to 8 are Private Slots, Number of Players and Voice Chat; Voice Chat moves up into row 6. The match is always
// two players in two public slots, which Find Match and Invite Player both join.
bool PatchCreateMatch(std::string& s) {
  constexpr const char* kSlotTexts[] = {"PrivateSlots_hilight",    "PrivateSlotsvalue_hilight",
                                        "PrivateSlots",            "PrivateSlotsvalue",
                                        "NumberofPlayers_hilight", "NumberofPlayersvalue_hilight",
                                        "NumberofPlayers",         "NumberofPlayersvalue"};
  bool ok = true;
  for (const char* row : {"numprivateslots_values", "numplayers_values"}) {
    ok = ok && Erase(s, "row", row) && Erase(s, "UIGrid", row);
  }
  for (const char* name : kSlotTexts) ok = ok && Hide(s, "UIText", name);
  for (const char* name : {"VoiceChat_hilight", "VoiceChat"}) {
    ok = ok && Move(s, "UIText", name, "<pos>-40 740 ", "<pos>-80 640 ");
  }
  for (const char* name : {"VoiceChatvalue_hilight", "VoiceChatvalue"}) {
    ok = ok && Move(s, "UIText", name, "<pos>560 745 ", "<pos>520 645 ");
  }
  return ok && EraseAnimEvent(s, "Highlight_CreateMatch_L7") && EraseAnimEvent(s, "Highlight_CreateMatch_L8") &&
         ReplaceOnce(s, "<anim>Highlight_CreateMatch_L9</anim>", "<anim>Highlight_CreateMatch_L7</anim>");
}

std::optional<Span> Screen(std::string_view xml, std::string_view screen) {
  constexpr std::string_view kOpen = "<UIScreen>";
  const std::string name_tag = NameTag(screen);
  std::optional<Span> found;
  for (size_t at = xml.find(name_tag); at != npos; at = xml.find(name_tag, at + 1)) {
    const size_t begin = xml.rfind(kOpen, at);
    if (begin == npos || !Blank(xml.substr(begin + kOpen.size(), at - begin - kOpen.size()))) continue;
    const size_t end = xml.find("</UIScreen>", at);
    if (end == npos || found) return std::nullopt;
    found = Span{begin, end};
  }
  return found;
}

bool PatchScreen(std::string& xml, std::string_view screen, bool (*patch)(std::string&)) {
  const std::optional<Span> span = Screen(xml, screen);
  if (!span) return false;
  std::string text = xml.substr(span->begin, span->end - span->begin);
  if (!patch(text)) return false;
  xml.replace(span->begin, span->end - span->begin, text);
  return true;
}

std::optional<Span> ScriptElement(std::string_view xml, std::string_view name) {
  const std::string open = "<script name=\"" + std::string(name) + "\">";
  const size_t begin = xml.find(open);
  if (begin == npos || xml.find(open, begin + 1) != npos) return std::nullopt;
  const size_t end = xml.find("</script>", begin);
  if (end == npos) return std::nullopt;
  return Span{begin, end};
}

// The closing brace matching the one at `open`, skipping /* */ comments.
size_t MatchingBrace(std::string_view text, size_t open, size_t limit) {
  int depth = 0;
  for (size_t at = open; at < limit; ++at) {
    if (text.compare(at, 2, "/*") == 0) {
      const size_t close = text.find("*/", at + 2);
      if (close == npos || close >= limit) return npos;
      at = close + 1;
    } else if (text[at] == '{') {
      ++depth;
    } else if (text[at] == '}' && --depth == 0) {
      return at;
    }
  }
  return npos;
}

// Erases the one `else if (<condition>) { ... }` branch of `script` whose block calls `call`.
bool EraseBranch(std::string& xml, std::string_view script, std::string_view condition, std::string_view call) {
  const std::optional<Span> span = ScriptElement(xml, script);
  if (!span) return false;
  const std::string head = "else if (" + std::string(condition) + ")";
  const size_t at = xml.find(head, span->begin);
  if (at == npos || at > span->end || xml.find(head, at + 1) < span->end) return false;
  const size_t open = xml.find('{', at);
  if (open == npos || !Blank(std::string_view(xml).substr(at + head.size(), open - at - head.size()))) return false;
  const size_t close = MatchingBrace(xml, open, span->end);
  if (close == npos || xml.substr(open, close - open).find(call) == npos) return false;
  EraseLines(xml, Span{at, close + 1});
  return true;
}

bool ReplaceInScript(std::string& xml, std::string_view script, std::string_view from, std::string_view to) {
  const std::optional<Span> span = ScriptElement(xml, script);
  return span && ReplaceIn(xml, *span, from, to);
}

uint8_t* HelpEntry(uint8_t* table, std::string_view screen) {
  const uint32_t hash = ScreenNameHash(screen);
  for (uint32_t i = 0; i < kHelpTextEntries; ++i) {
    uint8_t* entry = table + i * kHelpTextEntryBytes;
    if (LoadBE32(entry, 0) == hash) return entry;
  }
  return nullptr;
}

bool RemoveHelpRow(uint8_t* entry, uint32_t row) {
  const uint32_t rows = LoadBE32(entry, 4);
  if (row >= rows || rows > kHelpTextRows) return false;
  for (uint32_t i = row; i + 1 < rows; ++i) StoreBE32(entry, 12 + 4 * i, LoadBE32(entry, 12 + 4 * (i + 1)));
  StoreBE32(entry, 12 + 4 * (rows - 1), 0);
  StoreBE32(entry, 4, rows - 1);
  return true;
}

}  // namespace

std::optional<ScreenSetPatch> PatchScreenSet(std::string_view xml) {
  const size_t first = xml.find('<');
  if (first == npos || first > 8 || xml.substr(first, 13) != "<UIScreenSet>") return std::nullopt;
  ScreenSetPatch patch;
  patch.xml = std::string(xml);
  patch.multiplayer = PatchScreen(patch.xml, kMultiplayerScreen, PatchMultiplayer);
  patch.player_match = PatchScreen(patch.xml, kPlayerMatchScreen, PatchPlayerMatch);
  patch.create_match = PatchScreen(patch.xml, kCreatePlayerMatchScreen, PatchCreateMatch);
  if (!patch.multiplayer && !patch.player_match && !patch.create_match) return std::nullopt;
  return patch;
}

std::optional<LobbyScriptsPatch> PatchLobbyScripts(std::string_view xml) {
  if (!ScriptElement(xml, "GameLobbyUpdate")) return std::nullopt;
  LobbyScriptsPatch patch;
  patch.xml = std::string(xml);
  patch.gamer_card = EraseBranch(patch.xml, "GameLobbyUpdate", "IsNewlyPressedPF(BUTTON4, 0)", "ViewGamercard(");
  patch.players_list =
      EraseBranch(patch.xml, "GameLobbyUpdate", "IsNewlyPressedPF(LEFT_TRIGGER, 0)", "ShowPlayersUI(");
  patch.slot_help = ReplaceInScript(patch.xml, "GameLobbyUpdateHelpText",
                                    "UIText_SetString_TextID(selectionHelpText, tid_glGamertagSlot01);",
                                    "UIText_SetString_TextID(selectionHelpText, tid_blank);");
  if (!patch.gamer_card && !patch.players_list && !patch.slot_help) return std::nullopt;
  return patch;
}

uint32_t ScreenNameHash(std::string_view name) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const char c : name) {
    crc ^= static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

HelpTextPatch PatchHelpText(uint8_t* table) {
  HelpTextPatch patch;
  if (uint8_t* entry = HelpEntry(table, kMultiplayerScreen); entry && LoadBE32(entry, 4) == 4) {
    patch.multiplayer = RemoveHelpRow(entry, 3) && RemoveHelpRow(entry, 1);
  }
  if (uint8_t* entry = HelpEntry(table, kPlayerMatchScreen); entry && LoadBE32(entry, 4) == 3) {
    patch.player_match = RemoveHelpRow(entry, 1);
  }
  if (uint8_t* entry = HelpEntry(table, kCreatePlayerMatchScreen); entry && LoadBE32(entry, 4) == 9) {
    patch.create_match = RemoveHelpRow(entry, 7) && RemoveHelpRow(entry, 6);
  }
  return patch;
}

uint32_t HelpTextScreenText(const uint8_t* table, std::string_view screen) {
  const uint8_t* entry = HelpEntry(const_cast<uint8_t*>(table), screen);
  return entry ? LoadBE32(entry, 8) : 0;
}

}  // namespace ssf2t
