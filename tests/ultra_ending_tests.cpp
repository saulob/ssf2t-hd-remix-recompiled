// Ultra fighter endings: the package, the text, the routing, the screen and timeline patches, and the guest's own ending switch
// on both images.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "character/character_moves.h"
#include "character/evil_ryu_ending_text.h"
#include "character/fighter_identity.h"
#include "character/ultra_ending.h"
#include "character/ultra_hook_table.h"
#include "updater/third_party/miniz/miniz.h"

namespace {

using namespace ssf2t::character;
using F = FighterIdentity;

int failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::printf("FAIL %s\n", what);
  ++failures;
}

size_t Count(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
  return count;
}

std::string ReadText(const char* path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> Lines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

std::string Group(const std::string& xml, std::string_view name) {
  const size_t named = xml.find("<name>" + std::string(name) + "</name>");
  if (named == std::string::npos) return {};
  const size_t begin = xml.rfind("<UIElementGroup>", named);
  int depth = 0;
  for (size_t at = begin; at < xml.size();) {
    const size_t open = xml.find("<UIElementGroup>", at);
    const size_t close = xml.find("</UIElementGroup>", at);
    if (close == std::string::npos) return {};
    if (open != std::string::npos && open < close) {
      ++depth;
      at = open + 16;
    } else {
      --depth;
      at = close + 17;
      if (depth == 0) return xml.substr(begin, at - begin);
    }
  }
  return {};
}

bool Balanced(const std::string& xml) {
  for (const char* tag : {"UIText", "UIGraphic", "UIElementGroup"}) {
    if (Count(xml, std::string("<") + tag + ">") != Count(xml, std::string("</") + tag + ">")) return false;
  }
  return true;
}

void TestSpecs() {
  Check(kEvilRyuEnding.carrier == kRyuGuestCharacter && kViolentKenEnding.carrier == kKenGuestCharacter,
        "Evil Ryu rides on Ryu's ending case and Violent Ken on Ken's");
  Check(kEvilRyuEnding.group != kViolentKenEnding.group && std::string_view(kEvilRyuEnding.file) != kViolentKenEnding.file,
        "each ending has its own group and package");
  Check(kEvilRyuEnding.lines == 9 && kViolentKenEnding.lines == 10, "nine lines for Evil Ryu, ten for Violent Ken");
  Check(kEvilRyuEnding.line_ids == kEvilRyuEndingIds.data() && kViolentKenEnding.line_ids == kViolentKenEndingIds.data(),
        "each ending shows its own lines");

  Check(kEvilRyuEndingCutSeconds[1] * 60 == 1200 && kEvilRyuEndingCutSeconds[2] * 60 == 1800 &&
            kEvilRyuEndingCutSeconds[3] * 60 == 2400,
        "the cuts keep ed_chr18's frame numbers");
  Check(kEvilRyuEndingSeconds * 60 == 2700 && kEvilRyuEndingSeconds == kEvilRyuEndingLines * kEndingLineSeconds,
        "nine lines of 300 frames fill the 2700 frame scene");
  Check(kEndingSeconds * 60 == 3000 && kEndingCutSeconds[2] * 60 == 2100, "Violent Ken keeps ed_chr19's timing");

  Check(kEvilRyuEnding.guest_set_name == 0x82009088 && kEvilRyuEnding.guest_screen_name == 0x82009090 &&
            kEvilRyuEnding.guest_music == 0x23,
        "Evil Ryu plays in end_ryu of EndRyu with Ryu's music");
  Check(kViolentKenEnding.guest_set_name == 0x82009030 && kViolentKenEnding.guest_screen_name == 0x82009038 &&
            kViolentKenEnding.guest_music == 0x21,
        "Violent Ken plays in end_ken of EndKen with Ken's music");

  std::set<std::string_view> ids(kEvilRyuEndingIds.begin(), kEvilRyuEndingIds.end());
  Check(ids.size() == kEvilRyuEndingLines, "the ending ids are distinct");
  for (const std::string_view id : kViolentKenEndingIds) Check(!ids.contains(id), "no id is shared with Violent Ken's ending");
  for (const std::string_view id : {kEvilRyuTitleId, kShakunetsuId, kTatsumakiId, kViolentKenTitleId, kEvilRyuWinsId,
                                    kViolentKenWinsId, kShakunetsuCommandId, kAirTatsumakiId, kRasetsuId, kInazumaId,
                                    kShinbuId, kAirTatsumakiCommandId, kInazumaCommandId}) {
    Check(!ids.contains(id), "no id is shared with the pages or the banners");
  }

  std::set<uint32_t> used;
  const auto take = [&](uint32_t line) { Check(used.insert(line).second, "a table line is used once"); };
  for (const MoveListText& entry : kEvilRyuMoveListText) take(entry.line);
  for (const MoveListText& entry : kViolentKenMoveListText) take(entry.line);
  for (const uint32_t line : kViolentKenQuoteLines) take(line);
  for (const uint32_t line : kEvilRyuQuoteLines) take(line);
  for (const uint32_t line : kViolentKenEndingTableLines) take(line);
  take(kEvilRyuWinsLine);
  take(kViolentKenWinsLine);
  take(kAirTatsumakiCommandLine);
  for (const uint32_t line : kEvilRyuEndingTableLines) {
    take(line);
    Check(line > 44 && line < 62, "the ending lines are the Dhalsim and T.Hawk legacy lines");
  }
  for (const uint32_t line : {kRyuWinsLine, kKenWinsLine, kInAirLine}) {
    Check(!used.contains(line), "no ending line is a source line of the table");
  }
}

void TestText() {
  const std::array<std::string_view, 6> wins = {"RYU WINS", "RYU GAGNE", "RYU SIEGT", "RYU VINCE", "RYU GANA", "RYU VENCE"};
  for (const std::string_view banner : wins) {
    const auto& lines = EvilRyuEnding(banner);
    for (const std::string_view line : lines) {
      Check(!line.empty() && line.find_first_of("\r\n\t") == std::string_view::npos, "a line fits one table line");
    }
    Check(lines[0].find("Messatsu") != std::string_view::npos, "the scene opens with Messatsu");
    Check(&lines != &EvilRyuEnding("RYU WINS") || banner == "RYU WINS", "each language has its own row");
  }
  Check(EvilRyuEnding("RYU WINS")[0] == "\"Messatsu!\"" && EvilRyuEnding("RYU WINS")[1] == "\"I-Impossible!\"",
        "end_162 and end_163");
  Check(EvilRyuEnding("RYU WINS")[3] == "\"This is nothing... Defeating weaklings isn't enough for me now.\"",
        "a USF2 line break becomes a space");
  Check(EvilRyuEnding("RYU WINS")[8] == "\"Now...where is the demon that is worth a fight to the death?\"", "end_170");
  Check(EvilRyuEnding("RYU GAGNE")[2].find("insignifiant") != std::string_view::npos, "French end_164");
  Check(EvilRyuEnding("RYU SIEGT")[2].find("gebrochen") != std::string_view::npos, "German end_164");
  Check(EvilRyuEnding("RYU VINCE")[8].find("scontro mortale") != std::string_view::npos, "Italian end_170");
  Check(EvilRyuEnding("RYU GANA")[6] == "¡¡¡Wooooohhh!!!", "Spanish end_168 keeps its three marks");
  Check(EvilRyuEnding("SOMETHING ELSE") [0] == EvilRyuEnding("RYU WINS")[0], "an unknown language falls back to English");
  const auto& english = EvilRyuEnding("RYU WINS");
  const auto& portuguese = EvilRyuEnding("RYU VENCE");
  Check(&english != &portuguese, "pt-BR is a row of its own");
  for (size_t i = 0; i < portuguese.size(); ++i) Check(portuguese[i] != english[i] || i == 0 || i == 6, "pt-BR is translated");
  for (const std::string_view line : portuguese) {
    for (const std::string_view missing : {"\xC3\x80", "\xC3\x81", "\xC3\x82", "\xC3\x83"}) {
      Check(line.find(missing) == std::string_view::npos, "pt-BR uses no glyph the fonts lack");
    }
  }
}

void TestRouting() {
  const std::array<F, 2> p1{F::kEvilRyu, F::kGuest};
  const std::array<F, 2> p2{F::kGuest, F::kEvilRyu};
  const std::array<F, 2> none{F::kGuest, F::kGuest};
  using Cells = std::array<std::array<bool, 2>, kUltraEndings.size()>;
  const auto cells = [](bool evil0, bool evil1, bool violent0, bool violent1) {
    Cells value{};
    for (size_t slot = 0; slot < kUltraEndings.size(); ++slot) {
      const bool evil = kUltraEndings[slot]->identity == F::kEvilRyu;
      value[slot] = {evil ? evil0 : violent0, evil ? evil1 : violent1};
    }
    return value;
  };
  const std::array<uint32_t, 2> ryu_hover{0, 7};
  const std::array<uint32_t, 2> ryu_hover_right{7, 0};

  UltraEndingChoice choice = ChooseUltraEnding(0, p1, cells(true, false, false, false), ryu_hover);
  Check(choice.ending == &kEvilRyuEnding && choice.side == 0, "P1 Evil Ryu gets his ending on Ryu's case");
  choice = ChooseUltraEnding(0, p2, cells(false, true, false, false), ryu_hover_right);
  Check(choice.ending == &kEvilRyuEnding && choice.side == 1, "so does P2");
  Check(ChooseUltraEnding(0, none, cells(false, false, false, false), ryu_hover).ending == nullptr,
        "Ryu keeps Ryu's ending");
  Check(ChooseUltraEnding(0, p1, cells(false, false, false, false), ryu_hover).ending == nullptr,
        "leaving the cell gives Ryu's ending back");
  Check(ChooseUltraEnding(0, p1, cells(true, false, false, false), {7, 0}).ending == nullptr,
        "a request that is not on Ryu's side is no ending");
  Check(ChooseUltraEnding(4, p1, cells(true, false, false, false), {4, 7}).ending == nullptr,
        "Ken's win never plays Evil Ryu's ending");
  Check(ChooseUltraEnding(16, p1, cells(true, false, false, false), {16, 7}).ending == nullptr,
        "Akuma's win keeps Akuma's ending");
  Check(ChooseUltraEnding(5, p1, cells(true, false, false, false), ryu_hover).ending == nullptr,
        "another winner keeps that ending");

  const std::array<F, 2> violent{F::kViolentKen, F::kGuest};
  choice = ChooseUltraEnding(4, violent, cells(false, false, true, false), {4, 7});
  Check(choice.ending == &kViolentKenEnding && choice.side == 0, "Violent Ken still gets his ending on Ken's case");
  Check(ChooseUltraEnding(0, violent, cells(false, false, true, false), {4, 7}).ending == nullptr,
        "Violent Ken does not take Ryu's ending");
  Check(ChooseUltraEnding(4, p1, cells(true, false, false, false), {0, 7}).ending == nullptr,
        "Evil Ryu does not take Ken's ending");
  const std::array<F, 2> both{F::kEvilRyu, F::kViolentKen};
  choice = ChooseUltraEnding(4, both, cells(true, false, false, true), {0, 4});
  Check(choice.ending == &kViolentKenEnding && choice.side == 1, "each side's winner picks its own ending");
  choice = ChooseUltraEnding(0, both, cells(true, false, false, true), {0, 4});
  Check(choice.ending == &kEvilRyuEnding && choice.side == 0, "and Ryu's case belongs to Evil Ryu");
}

void TestPatches() {
  const std::string ryu_screen =
      "<UIScreenSet><UIScreen><name>EndRyu</name><uiElements><UIElementGroup><name>end_ryu</name></UIElementGroup>\n"
      "</uiElements></UIScreen></UIScreenSet>";
  std::string screen = ryu_screen;
  Check(!PatchEndingScreen(screen, kViolentKenEnding) && screen == ryu_screen, "EndRyu is not a Violent Ken document");
  Check(PatchEndingScreen(screen, kEvilRyuEnding), "EndRyu takes the Evil Ryu group");
  Check(!PatchEndingScreen(screen, kEvilRyuEnding) && !PatchEndingScreen(screen), "only once");
  Check(screen.find("<name>end_ryu</name>") == ryu_screen.find("<name>end_ryu</name>"), "Ryu's group stays in place");
  Check(Count(screen, "<UIGraphic>") == kEndingCuts && Count(screen, "<UIText>") == kEvilRyuEndingLines,
        "four cuts and nine lines");
  Check(Count(screen, "<UIElementGroup>") == 2 && Count(screen, "</UIElementGroup>") == 2, "balanced");
  Check(screen.find(EndingFrameName(kEvilRyuEnding, 3)) != std::string::npos &&
            EndingFrameName(kEvilRyuEnding, 3) == "end_evilryu_frame4",
        "the hook finds the last frame by name");
  for (const std::string_view id : kEvilRyuEndingIds) {
    Check(screen.find("<textID>" + std::string(id) + "</textID>") != std::string::npos, "each line is shown");
  }
  Check(Count(screen, "end_violentken") == 0, "no Violent Ken name in Ryu's screen");

  std::string ken_screen =
      "<UIScreenSet><UIScreen><name>EndKen</name><uiElements><UIElementGroup><name>end_ken</name></UIElementGroup>\n"
      "</uiElements></UIScreen></UIScreenSet>";
  Check(PatchEndingScreen(ken_screen) && Count(ken_screen, "end_evilryu") == 0 && Count(ken_screen, "<UIText>") == 10,
        "EndKen takes only Violent Ken's group through the generic entry");
  std::string other = "<UIScreenSet></UIScreenSet>";
  Check(!PatchEndingScreen(other) && other == "<UIScreenSet></UIScreenSet>", "another screen set is left alone");

  const std::string both_animations =
      "<UIElementDescriptors><UIAnim><name>end_ryu</name></UIAnim><UIAnim><name>end_ken</name></UIAnim>"
      "<UIAnim><name>end_short_fadein</name></UIAnim>\n</UIElementDescriptors>";
  std::string animations = both_animations;
  Check(PatchEndingAnimations(animations), "the descriptors take the timelines");
  Check(!PatchEndingAnimations(animations), "only once");
  Check(animations.find("<name>end_evilryu</name>") != std::string::npos &&
            animations.find("<name>end_violentken</name>") != std::string::npos,
        "both timelines are there");
  Check(Count(animations, "GameFlowSignal(cinematic_done);") == 2, "each scene ends the native way");
  Check(animations.find("<totalTime>45.000</totalTime>") != std::string::npos &&
            animations.find("<totalTime>50.000</totalTime>") != std::string::npos,
        "Evil Ryu's scene lasts 45 seconds, Violent Ken's 50");
  std::string single = both_animations;
  Check(PatchEndingAnimations(single, kEvilRyuEnding) && Count(single, "end_violentken") == 0, "one timeline at a time");
  Check(single.find("<time>30.000</time>\n<eventName>end_evilryu_fm3fadein</eventName>") != std::string::npos,
        "the third cut starts at frame 1800");
  Check(single.find("<time>44.900</time>\n<scriptName>GameFlowSignal(cinematic_done);") != std::string::npos,
        "the scene signals its end just before frame 2700");
  std::string only_ken = "<UIElementDescriptors><UIAnim><name>end_ken</name></UIAnim><UIAnim><name>end_short_fadein</name></UIAnim>"
                         "</UIElementDescriptors>";
  Check(PatchEndingAnimations(only_ken) && Count(only_ken, "end_evilryu") == 0, "a document without end_ryu gets no Evil Ryu timeline");
  other = "<UIElementDescriptors></UIElementDescriptors>";
  Check(!PatchEndingAnimations(other), "another descriptor file is left alone");

  for (int line = 1; line <= kEvilRyuEndingLines; ++line) {
    const std::string prefix = "end_evilryu_tx" + std::to_string(line);
    Check(Count(single, prefix + "fadein") == 1 && Count(single, prefix + "fadeout") == 1 &&
              Count(screen, prefix + "fadein") == 1 && Count(screen, prefix + "fadeout") == 1,
          "every text event has one key and one listener");
  }
  for (int cut = 1; cut <= kEndingCuts; ++cut) {
    const std::string prefix = "end_evilryu_fm" + std::to_string(cut);
    Check(Count(single, prefix + "fadein") == 1 && Count(screen, prefix + "fadein") == 1,
          "every cut event has one key and one listener");
  }
}

void TestPackage() {
  std::ifstream package("game/extracted/evil_ryu_ending.bin", std::ios::binary);
  if (!package) {
    std::printf("note: game/extracted/evil_ryu_ending.bin not found, package checks skipped\n");
    return;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(package)), std::istreambuf_iterator<char>());
  UltraEndingArt art;
  Check(ParseUltraEndingArt(bytes.data(), bytes.size(), &art), "the generated ending package parses");
  Check(art.pixels.size() == kEndingCutBytes * kEndingCuts, "four cuts");
  for (int cut = 0; cut < kEndingCuts; ++cut) Check(art.Cut(cut)[0] == 0xFF, "every cut is opaque A,R,G,B");
  Check(!ParseUltraEndingArt(bytes.data(), bytes.size() - 1, &art), "a short package is refused");
  std::vector<uint8_t> wrong = bytes;
  wrong[0] = 'X';
  Check(!ParseUltraEndingArt(wrong.data(), wrong.size(), &art), "a package of another format is refused");
  std::vector<uint8_t> three = bytes;
  three[8] = 3;
  Check(!ParseUltraEndingArt(three.data(), three.size(), &art), "a package with another cut count is refused");

  std::ifstream violent("game/extracted/violent_ken_ending.bin", std::ios::binary);
  if (!violent) return;
  const std::vector<uint8_t> other((std::istreambuf_iterator<char>(violent)), std::istreambuf_iterator<char>());
  Check(other.size() == bytes.size() && !std::equal(bytes.begin() + 12, bytes.begin() + 12 + 4096, other.begin() + 12),
        "the two packages hold different art");
}

struct Archive {
  std::array<size_t, kEvilRyuEndingLines> users{};
  std::vector<std::string> ids;
  std::vector<std::vector<std::string>> tables;
  std::string screen_ryu, screen_ken, animations;
};

bool LoadArchive(Archive* out) {
  std::ifstream in("game/extracted/flog_x360.fpg", std::ios::binary);
  if (!in) return false;
  uint32_t count = 0;
  in.seekg(4);
  in.read(reinterpret_cast<char*>(&count), 4);
  std::vector<unsigned char> data(8 << 20);
  for (uint32_t slot = 0; slot < count; ++slot) {
    uint32_t entry[4];
    in.seekg(0x800 + slot * 16);
    in.read(reinterpret_cast<char*>(entry), 16);
    if (!in || entry[2] == 0 || entry[2] > 4000000) continue;
    std::vector<unsigned char> stored(entry[2]);
    in.seekg(entry[1]);
    in.read(reinterpret_cast<char*>(stored.data()), entry[2]);
    mz_ulong size = mz_ulong(data.size());
    if (mz_uncompress(data.data(), &size, stored.data(), mz_ulong(stored.size())) != MZ_OK) continue;
    const std::string text(reinterpret_cast<const char*>(data.data()), size);
    if (text.rfind("1989\t1\n// Ryu's Moves\nRyu_Hadouken", 0) == 0) out->ids = Lines(text);
    else if (text.rfind("1989\t1\n// Ryu's Moves\ntid_Ryu_", 0) == 0) out->tables.push_back(Lines(text));
    else if (text.rfind("<UIScreenSet>", 0) == 0 && text.find("<name>EndRyu</name>") != std::string::npos) out->screen_ryu = text;
    else if (text.rfind("<UIScreenSet>", 0) == 0 && text.find("<name>EndKen</name>") != std::string::npos) out->screen_ken = text;
    else if (text.rfind("<UIElementDescriptors>", 0) == 0 && text.find("<name>end_ryu</name>") != std::string::npos &&
             text.find("end_short_fadein") != std::string::npos) out->animations = text;
    if (text.rfind("1989\t1\n// Ryu's Moves\n", 0) == 0) continue;
    for (size_t i = 0; i < kEvilRyuEndingLines; ++i) out->users[i] += Count(text, kEvilRyuEndingIds[i]);
  }
  return !out->ids.empty() && out->tables.size() == 5 && !out->screen_ryu.empty() && !out->screen_ken.empty() &&
         !out->animations.empty();
}

void TestRealData() {
  Archive archive;
  if (!LoadArchive(&archive)) {
    std::printf("note: game/extracted/flog_x360.fpg not found, real-data checks skipped\n");
    return;
  }
  for (size_t i = 0; i < kEvilRyuEndingLines; ++i) {
    Check(archive.ids[kEvilRyuEndingTableLines[i]] == kEvilRyuEndingIds[i], "each ending line belongs to its id");
    Check(archive.users[i] == 0, "no screen, script or other entry of the guest names an ending id");
  }

  for (size_t language = 0; language < archive.tables.size(); ++language) {
    std::string table;
    for (const std::string& line : archive.tables[language]) table += line + "\n";
    std::string patched = table;
    PatchMoveListText(patched);
    const std::vector<std::string> after = Lines(patched);
    Check(after.size() == archive.tables[language].size(), "the table keeps its line count");
    const auto& english = archive.tables[language];
    for (size_t i = 0; i < kEvilRyuEndingLines; ++i) {
      const uint32_t line = kEvilRyuEndingTableLines[i];
      Check(after[line] == EvilRyuEnding(english[kRyuWinsLine])[i], "the table carries the ending in its language");
    }
    for (size_t line = 0; line < english.size(); ++line) {
      const bool ending = std::find(kEvilRyuEndingTableLines.begin(), kEvilRyuEndingTableLines.end(), line) !=
                          kEvilRyuEndingTableLines.end();
      if (!ending) continue;
      Check(after[line] != english[line], "an ending line is replaced");
    }
    std::string portuguese = table;
    const size_t at = portuguese.find(english[kRyuWinsLine] + "\n");
    Check(at != std::string::npos, "the banner is in the table");
    if (at != std::string::npos) {
      portuguese.replace(at, english[kRyuWinsLine].size(), "RYU VENCE");
      PatchMoveListText(portuguese);
      Check(Lines(portuguese)[kEvilRyuEndingTableLines[0]] == "\"Messatsu!\"" &&
                Lines(portuguese)[kEvilRyuEndingTableLines[8]] == EvilRyuEnding("RYU VENCE")[8],
            "a table with the pt-BR wording takes the pt-BR row");
    }
  }

  std::string screen = archive.screen_ryu;
  std::string animations = archive.animations;
  Check(PatchEndingScreen(screen), "the real EndRyu document takes the group");
  Check(PatchEndingAnimations(animations), "the real ending animations take the timelines");
  Check(Balanced(screen) && Count(screen, "<UIScreen>") == 1, "the real document stays balanced");
  Check(Group(screen, "end_ryu") == Group(archive.screen_ryu, "end_ryu"), "Ryu's ending is byte for byte unchanged");
  const size_t head = archive.screen_ryu.find("<UIElementGroup>");
  Check(screen.compare(0, head, archive.screen_ryu, 0, head) == 0, "the head of the screen set is unchanged");
  Check(Count(screen, "<UIGraphic>") == Count(archive.screen_ryu, "<UIGraphic>") + kEndingCuts &&
            Count(screen, "<UIText>") == Count(archive.screen_ryu, "<UIText>") + kEvilRyuEndingLines,
        "four cuts and nine lines were added");
  const std::string group = Group(screen, "end_evilryu");
  Check(!group.empty() && Count(screen, "<name>end_evilryu</name>") == 1, "the Evil Ryu group is there once");
  Check(Count(animations, "<name>end_ryu</name>") == Count(archive.animations, "<name>end_ryu</name>"),
        "Ryu's own timeline stays");
  const size_t kept = archive.animations.size() - 25;
  Check(animations.compare(0, kept, archive.animations, 0, kept) == 0, "the guest's animations are byte for byte unchanged");
  for (int cut = 1; cut <= kEndingCuts; ++cut) {
    const std::string fade = "end_evilryu_fm" + std::to_string(cut) + "fadein";
    Check(Count(animations, fade) == 1 && Count(screen, fade) == 1, "the cut events pair up in the real documents");
  }
  Check(Count(archive.screen_ryu, "end_short_fadein") > 0 && Count(archive.animations, "<name>end_short_fadeout</name>") == 1,
        "the fades the group uses exist in the real documents");

  std::string ken = archive.screen_ken;
  Check(PatchEndingScreen(ken) && Count(ken, "end_evilryu") == 0 && Count(ken, "end_violentken_frame1") > 0,
        "Ken's document still takes only the Violent Ken group");
}

enum Image { kBase013, kTitleUpdate023 };

struct Build {
  const char* name;
  const char* file;
  uint32_t play_ending;
};

constexpr Build kBuilds[] = {
    {"Base013", "game/analysis/default-0.0.1.3.image.bin", 0x823CBE80},
    {"TU023", "game/analysis/default.image.bin", 0x823CC460},
};

constexpr uint32_t kImageBase = 0x82000000;

void TestGuest(const Build& build) {
  std::ifstream in(build.file, std::ios::binary);
  if (!in) {
    std::printf("note: %s not found, %s fingerprints skipped\n", build.file, build.name);
    return;
  }
  const std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto word = [&](uint32_t address) {
    const size_t at = address - kImageBase;
    return uint32_t(image[at]) << 24 | uint32_t(image[at + 1]) << 16 | uint32_t(image[at + 2]) << 8 | image[at + 3];
  };
  const auto half = [&](uint32_t address) {
    const size_t at = address - kImageBase;
    return uint32_t(image[at]) << 8 | image[at + 1];
  };
  const auto text = [&](uint32_t address, size_t size) {
    return std::string(image.begin() + (address - kImageBase), image.begin() + (address - kImageBase) + size);
  };
  const auto absolute = [&](uint32_t lis, uint32_t addi) {
    return uint32_t(int32_t(int16_t(word(lis))) << 16) + uint32_t(int32_t(int16_t(word(addi))));
  };
  const uint32_t fn = build.play_ending;

  Check(word(fn + 0x24) == 0x2B0B0010, "the switch has 17 cases");
  Check((word(fn + 0x30) >> 16) == 0x3D80 && (word(fn + 0x34) >> 16) == 0x398C, "the jump table address is built in r12");
  const uint32_t table = absolute(fn + 0x30, fn + 0x34) + 0;
  Check(table == 0x82008D78, "the case offsets are the table both images share");
  Check((word(fn + 0x40) >> 16) == 0x3D80 && (word(fn + 0x44) >> 16) == 0x398C, "the case base is built in r12");
  const uint32_t cases = absolute(fn + 0x40, fn + 0x44);
  Check(cases == fn + 0x58, "the cases follow the dispatch");
  Check(word(fn + 0x4C) == 0x7D8903A6 && word(fn + 0x54) == 0x4E800420, "the dispatch is mtctr / bctr");

  Check(half(table + 0) == 0, "character 0 is the first case");
  const uint32_t ryu = cases + half(table + 0);
  Check(half(table + 8) != 0 && cases + half(table + 8) != ryu, "Ken's case is another one");
  const uint32_t ken = cases + half(table + 2 * kKenGuestCharacter);
  Check(word(ken + 0x60) == 0x38600021, "Ken's case asks for Ken's music");
  Check(word(ryu + 0x60) == 0x38600023 && word(ryu + 0x68) == 0x38600023, "Ryu's case asks for Ryu's music twice");
  Check(word(ryu) == 0x3D608201 && uint16_t(word(ryu + 4)) == uint16_t(kEvilRyuEnding.guest_screen_name),
        "Ryu's case names the EndRyu literal the hook uses");
  Check(word(ryu + 0x10) == 0x3D608201 && uint16_t(word(ryu + 0x14)) == uint16_t(kEvilRyuEnding.guest_set_name),
        "Ryu's case names the end_ryu literal the hook uses");
  Check(word(ken) == 0x3D608201 && uint16_t(word(ken + 4)) == uint16_t(kViolentKenEnding.guest_screen_name),
        "Ken's case names the EndKen literal");
  Check(text(0x82009090, 7) == std::string("EndRyu\0", 7) && text(0x82009088, 8) == std::string("end_ryu\0", 8),
        "the Evil Ryu literals read EndRyu and end_ryu");
  Check(text(0x82009038, 7) == std::string("EndKen\0", 7) && text(0x82009030, 8) == std::string("end_ken\0", 8),
        "the Violent Ken literals read EndKen and end_ken");
  const uint32_t music_prepare = ryu + 0x64;
  const uint32_t music_play = ryu + 0x6C;
  Check((word(music_prepare) >> 26) == 18 && (word(music_play) >> 26) == 18, "both music calls are calls");
  const auto call_target = [&](uint32_t at) {
    const uint32_t li = word(at);
    return at + (li & 0x03FFFFFC) - (li & 0x02000000 ? 0x04000000 : 0);
  };
  Check(call_target(music_prepare) == call_target(ken + 0x64) && call_target(music_play) == call_target(ken + 0x6C),
        "Ryu and Ken call the same music functions");
}

void TestRegistration() {
  Check(Base013UltraHooks::kPlayEnding1 == kBuilds[kBase013].play_ending &&
            TitleUpdate023UltraHooks::kPlayEnding1 == kBuilds[kTitleUpdate023].play_ending,
        "the ending hook is registered for both images");
  const std::string hook = ReadText("src/character/ultra_ending_hook.cpp");
  Check(Count(hook, "REX_HOOK_RAW(base013_sub_823CBE80)") == 1 && Count(hook, "REX_HOOK_RAW(tu023_sub_823CC460)") == 1,
        "both images hook PlayCinematic once");
  Check(Count(hook, "ChooseUltraEnding(") == 1 && Count(hook, "kUltraEndings") >= 1,
        "the hook routes through the shared choice");
  Check(Count(hook, "DEBUG_SAULO") == 2, "one diagnostic per ending and one when it falls back");
  const std::string parity = ReadText("src/feature_parity.h");
  Check(Count(parity, "UltraHooks::kPlayEnding1") >= 2 && Count(parity, "src/character/ultra_ending_hook.cpp") >= 1 &&
            Count(parity, "tests/ultra_ending_tests.cpp") == 1,
        "the feature registry lists the hook, its source and this suite");
  const std::string ps1 = ReadText("tools/test.ps1");
  const std::string sh = ReadText("tools/test.sh");
  Check(Count(ps1, "'ultra_ending'") == 1 && Count(sh, "ssf2t_ultra_ending_tests") == 1, "the suite is registered in both runners");
}

}  // namespace

int main() {
  TestSpecs();
  TestText();
  TestRouting();
  TestPatches();
  TestPackage();
  TestRealData();
  TestGuest(kBuilds[kBase013]);
  TestGuest(kBuilds[kTitleUpdate023]);
  TestRegistration();
  if (failures == 0) std::printf("ultra ending tests passed\n");
  return failures == 0 ? 0 : 1;
}
