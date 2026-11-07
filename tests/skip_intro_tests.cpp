// Skip intro: setting, launcher row, diagnostics, the boot logo skip and the gameflow FSM patch, checked on synthetic
// input and on the real documents and images of both game builds. Headless; the real-data checks are skipped when the game files are not on this machine.
// Run: ssf2t_skip_intro_tests

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "skip_intro.h"
#include "updater/third_party/miniz/miniz.h"

namespace {

namespace fs = std::filesystem;

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

fs::path Root() { return fs::path(SSF2T_SOURCE_DIR); }

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string ReadSource(const char* name) { return ReadText(Root() / "src" / name); }

bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

size_t Count(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
  return count;
}

std::string Between(const std::string& text, const char* begin, const char* end) {
  const size_t start = text.find(begin);
  if (start == std::string::npos) return {};
  const size_t stop = text.find(end, start);
  return stop == std::string::npos ? std::string() : text.substr(start, stop - start);
}

std::string StripCarriageReturns(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c != '\r') out += c;
  }
  return out;
}

const char kGameflow[] =
    "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<FSM name=\"gameflow_fsm\">\n"
    "\t<State name=\"gameflow_fsm_LegalText_i000\" timeout=\"5.000000\">\n"
    "\t\t<Transition dst=\"gameflow_fsm_FadeOutLegal_i000\"><signal>timeout</signal></Transition>\n"
    "\t</State>\n"
    "\t<State name=\"gameflow_fsm_OpenBeta_i000\">\n"
    "\t\t<Transition dst=\"gameflow_fsm_Title_Screen_i000\"><signal>return true;</signal></Transition>\n"
    "\t</State>\n"
    "\t<State name=\"gameflow_fsm_Title_Screen_i000\">\n"
    "\t\t<Transition dst=\"gameflow_fsm_background_i000\"><signal>title_to_main</signal></Transition>\n"
    "\t</State>\n"
    "\t<EmptyNode name=\"initial\"><Transition dst=\"gameflow_fsm_load_screenset_i000\" /></EmptyNode>\n"
    "\t<State name=\"gameflow_fsm_load_screenset_i000\">\n"
    "\t\t<Transition dst=\"gameflow_fsm_LegalText_i000\">\n"
    "\t\t\t<signal>IsUISlotActive(0);</signal>\n"
    "\t\t\t<action>UIEngine_ActivateUIScreenSet(0);</action>\n"
    "\t\t</Transition>\n"
    "\t</State>\n"
    "</FSM>\n";

void TestSetting() {
  std::printf("skip intro setting\n");
  CHECK(Has(ReadSource("settings.h"), "REXCVAR_DECLARE(bool, ssf2t_skip_intro);"));
  CHECK(Has(ReadSource("settings.cpp"),
            "REXCVAR_DEFINE_BOOL(ssf2t_skip_intro, false, \"SSF2T\", \"Start directly at the title screen\");"));
  const std::string settings = ReadSource("settings.cpp");
  const std::string keys = Between(settings, "std::vector<std::string> GameSettingKeys() {", "  return keys;");
  CHECK(Has(keys, "\"ssf2t_skip_intro\","));
  CHECK(Has(settings, "for (const std::string& key : kGameSettingKeys) {"));
  CHECK(!Has(ReadText(Root() / "config/ssf2t_hd_remix.toml"), "ssf2t_skip_intro"));
}

void TestLauncherRow() {
  std::printf("skip intro launcher row\n");
  const std::string launcher = ReadSource("launcher.cpp");
  const std::string page = Between(launcher, "static void PageGameplay(trg::Ui& ui) {", "if (DeveloperControlsEnabled())");
  CHECK(!page.empty());
  const size_t language = page.find("ui.Combo(kLanguageLabel");
  const size_t skip = page.find("ui.Toggle(\"Skip intro\"");
  const size_t frame_rate = page.find("ui.Toggle(\"Frame rate in title\"");
  CHECK(language != std::string::npos && skip != std::string::npos && frame_rate != std::string::npos);
  CHECK(language < skip && skip < frame_rate);
  CHECK(Has(page,
            "ui.Toggle(\"Skip intro\", \"Start directly at the title screen\", \"ssf2t_skip_intro\", false, \"Off\", "
            "\"On\");"));
  CHECK(Count(launcher, "ssf2t_skip_intro") == 1);
  CHECK(Has(page, "\"ssf2t_show_fps\", true, \"Off\", \"On\""));
  CHECK(Has(page, "\"ssf2t_show_frame_counter\", false, \"Hidden\", \"Shown\""));
  const std::string translations = ReadSource("localization/pt_br_ui_text.cpp");
  CHECK(Has(translations, "Skip intro\t"));
  CHECK(Has(translations, "Start directly at the title screen\t"));
}

void TestDiagnostics() {
  std::printf("skip intro diagnostics\n");
  CHECK(Has(ReadSource("settings.cpp"),
            "diagnostics.launcher_gameplay.skip_intro = OnOff(settings.GetBool(\"ssf2t_skip_intro\", false));"));
  CHECK(Has(ReadSource("diagnostics.h"), "std::string skip_intro;"));
  const std::string diagnostics = ReadSource("diagnostics.cpp");
  const size_t language = diagnostics.find("\"Launcher Gameplay: Language=\"");
  const size_t skip = diagnostics.find("\" | Skip intro=\" + diagnostics.launcher_gameplay.skip_intro");
  const size_t title = diagnostics.find("\" | Frame rate in title=\"");
  CHECK(language != std::string::npos && language < skip && skip < title);
}

void TestPatch() {
  std::printf("skip intro gameflow patch\n");
  const std::string original = kGameflow;
  const std::optional<std::string> patched = ssf2t::PatchGameflowIntro(original);
  CHECK(patched.has_value());
  if (!patched) return;
  CHECK(patched->size() + 1 == original.size());
  CHECK(Count(*patched, "dst=\"gameflow_fsm_LegalText_i000\"") == 0);
  CHECK(Count(original, "dst=\"gameflow_fsm_LegalText_i000\"") == 1);
  const std::string boot = Between(*patched, "<State name=\"gameflow_fsm_load_screenset_i000\">", "</State>");
  CHECK(Has(boot, "dst=\"gameflow_fsm_OpenBeta_i000\""));
  CHECK(Has(boot, "<signal>IsUISlotActive(0);</signal>"));
  CHECK(Has(boot, "<action>UIEngine_ActivateUIScreenSet(0);</action>"));

  std::string restored = *patched;
  const size_t at = restored.find("dst=\"gameflow_fsm_OpenBeta_i000\"", restored.find("load_screenset_i000\">"));
  restored.replace(at, 32, "dst=\"gameflow_fsm_LegalText_i000\"");
  CHECK(restored == original);

  CHECK(!ssf2t::PatchGameflowIntro(*patched).has_value());
}

void TestPatchFailsSafe() {
  std::printf("skip intro patch leaves other documents whole\n");
  CHECK(!ssf2t::PatchGameflowIntro("").has_value());
  CHECK(!ssf2t::PatchGameflowIntro("<UIScreenSet></UIScreenSet>").has_value());
  CHECK(!ssf2t::PatchGameflowIntro("<FSM name=\"player_match_fsm\"></FSM>").has_value());
  CHECK(!ssf2t::PatchGameflowIntro("<FSM name=\"gameflow_fsm\"></FSM>").has_value());

  for (const char* required : {"<State name=\"gameflow_fsm_LegalText_i000\"", "<State name=\"gameflow_fsm_OpenBeta_i000\"",
                               "<State name=\"gameflow_fsm_Title_Screen_i000\"",
                               "<State name=\"gameflow_fsm_load_screenset_i000\">"}) {
    std::string without = kGameflow;
    without.replace(without.find(required), 7, "<Statx ");
    CHECK(!ssf2t::PatchGameflowIntro(without).has_value());
  }

  std::string elsewhere = kGameflow;
  elsewhere.replace(elsewhere.find("dst=\"gameflow_fsm_LegalText_i000\""), 32, "dst=\"gameflow_fsm_Title_Screen_i000\"");
  elsewhere.insert(elsewhere.find("</FSM>"), "<State name=\"x\"><Transition dst=\"gameflow_fsm_LegalText_i000\"/></State>\n");
  CHECK(!ssf2t::PatchGameflowIntro(elsewhere).has_value());

  std::string twice = kGameflow;
  twice.insert(twice.find("\t\t</Transition>"), "<Transition dst=\"gameflow_fsm_LegalText_i000\"/>");
  CHECK(!ssf2t::PatchGameflowIntro(twice).has_value());

  std::string late_header = std::string(300, ' ') + kGameflow;
  CHECK(!ssf2t::PatchGameflowIntro(late_header).has_value());
  CHECK(ssf2t::IsGameflowFsmHead("<?xml version=\"1.0\"?>\n<FSM name=\"gameflow_fsm\">"));
  CHECK(!ssf2t::IsGameflowFsmHead("<FSM name=\"gameflow_fsm_other\">"));
}

bool ReadEntry(std::ifstream& in, uint32_t slot, uint32_t key, std::string* out) {
  uint32_t entry[4] = {};
  in.seekg(0x800 + slot * 16);
  in.read(reinterpret_cast<char*>(entry), 16);
  if (!in || entry[0] != key || entry[2] == 0 || entry[2] > 4000000) return false;
  std::vector<unsigned char> stored(entry[2]);
  in.seekg(entry[1]);
  in.read(reinterpret_cast<char*>(stored.data()), entry[2]);
  std::vector<unsigned char> data(8 << 20);
  mz_ulong size = mz_ulong(data.size());
  if (mz_uncompress(data.data(), &size, stored.data(), mz_ulong(stored.size())) != MZ_OK) return false;
  out->assign(reinterpret_cast<const char*>(data.data()), size);
  return true;
}

void TestRealDocument() {
  std::ifstream in(Root() / "game/extracted/flog_x360.fpg", std::ios::binary);
  std::string document;
  if (!in || !ReadEntry(in, 453, 0xC05679BB, &document)) {
    std::printf("note: game/extracted/flog_x360.fpg is not on this machine, the real document is not checked\n");
    return;
  }
  std::printf("skip intro on the game's own gameflow FSM\n");
  const std::string original = StripCarriageReturns(document);
  CHECK(ssf2t::IsGameflowFsmHead(original.substr(0, ssf2t::kGameflowHead)));
  CHECK(Count(original, "dst=\"gameflow_fsm_LegalText_i000\"") == 1);
  const std::optional<std::string> patched = ssf2t::PatchGameflowIntro(original);
  CHECK(patched.has_value());
  if (!patched) return;

  const std::string boot = Between(*patched, "<State name=\"gameflow_fsm_load_screenset_i000\">", "</State>");
  CHECK(Has(boot, "dst=\"gameflow_fsm_OpenBeta_i000\""));
  CHECK(Has(boot, "IsUISlotActive(0);") && Has(boot, "UIEngine_ActivateUIScreenSet(0);"));
  CHECK(Count(*patched, "dst=\"gameflow_fsm_LegalText_i000\"") == 0);
  CHECK(patched->size() + 1 == original.size());

  std::string restored = *patched;
  const size_t at = restored.find("dst=\"gameflow_fsm_OpenBeta_i000\"", restored.find("load_screenset_i000\">"));
  restored.replace(at, 32, "dst=\"gameflow_fsm_LegalText_i000\"");
  CHECK(restored == original);

  const std::string open_beta = Between(*patched, "<State name=\"gameflow_fsm_OpenBeta_i000\">", "</State>");
  CHECK(Has(open_beta, "dst=\"gameflow_fsm_Title_Screen_i000\""));
  CHECK(!Has(open_beta, "gameflow_fsm_Main_Menu"));

  const std::string title = Between(*patched, "<State name=\"gameflow_fsm_Title_Screen_i000\">", "</State>\n\t<!--To Main Menu");
  CHECK(Has(title, "UIScreen_Push(TitleScreen);"));
  CHECK(Has(title, "IsNewlyPressedPF(START, 0);"));
  CHECK(Has(title, "<signal>\n\t\t\t\ttitle_to_main\n\t\t\t</signal>"));
  CHECK(title == Between(original, "<State name=\"gameflow_fsm_Title_Screen_i000\">", "</State>\n\t<!--To Main Menu"));

  for (const char* state : {"<State name=\"gameflow_fsm_LegalText_i000\"", "<State name=\"gameflow_fsm_FadeOutLegal_i000\"",
                            "<State name=\"gameflow_fsm_Logo_i000\"", "<State name=\"gameflow_fsm_Main_Menu_control_i000\""}) {
    CHECK(Count(*patched, state) == 1);
  }
  CHECK(Has(original, "<EmptyNode name=\"initial\">") &&
        Has(Between(original, "<EmptyNode name=\"initial\">", "</EmptyNode>"), "dst=\"gameflow_fsm_load_screenset_i000\""));
}

struct Build {
  const char* tree;
  const char* image;
  const char* hook_symbol;
  uint32_t parse;
  uint32_t step;
  uint32_t state;
  uint32_t finished;
  uint32_t capcom;
  uint32_t backbone;
  uint32_t ref_count;
  uint32_t release;
  uint32_t game_flow_init;
};

constexpr Build kBuilds[] = {
    {"base", "game/analysis/default-0.0.1.3.image.bin", "base013", ssf2t::Base013SkipIntro::kXmlDocumentParse,
     ssf2t::Base013SkipIntro::kBootLogoStep, ssf2t::Base013SkipIntro::kBootLogoState,
     ssf2t::Base013SkipIntro::kBootLogoFinished, ssf2t::Base013SkipIntro::kCapcomLogo,
     ssf2t::Base013SkipIntro::kBackboneLogo, ssf2t::Base013SkipIntro::kTextureRefCount,
     ssf2t::Base013SkipIntro::kTextureRelease, 0x823BC7A0},
    {"tu", "game/analysis/default.image.bin", "tu023", ssf2t::TitleUpdate023SkipIntro::kXmlDocumentParse,
     ssf2t::TitleUpdate023SkipIntro::kBootLogoStep, ssf2t::TitleUpdate023SkipIntro::kBootLogoState,
     ssf2t::TitleUpdate023SkipIntro::kBootLogoFinished, ssf2t::TitleUpdate023SkipIntro::kCapcomLogo,
     ssf2t::TitleUpdate023SkipIntro::kBackboneLogo, ssf2t::TitleUpdate023SkipIntro::kTextureRefCount,
     ssf2t::TitleUpdate023SkipIntro::kTextureRelease, 0x823BC728},
};

constexpr uint32_t kImageBase = 0x82000000;

struct Image {
  std::vector<uint8_t> bytes;

  bool Load(const char* path) {
    std::ifstream in(Root() / path, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return bytes.size() > 0x100000;
  }

  uint32_t Word(uint32_t address) const {
    const size_t at = address - kImageBase;
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 | uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
  }

  std::string CString(uint32_t address) const {
    return reinterpret_cast<const char*>(&bytes[address - kImageBase]);
  }
};

int32_t Signed16(uint32_t word) { return int16_t(word & 0xFFFF); }

uint32_t Opcode(uint32_t word) { return word >> 26; }

uint32_t RegisterA(uint32_t word) { return (word >> 16) & 31; }

// The address a load or store reaches through a lis in the few words before it.
std::optional<uint32_t> EffectiveAddress(const Image& image, uint32_t at) {
  const uint32_t word = image.Word(at);
  if (RegisterA(word) == 0 || (Opcode(word) != 32 && Opcode(word) != 34 && Opcode(word) != 36 && Opcode(word) != 38)) {
    return std::nullopt;
  }
  for (uint32_t back = 4; back <= 16; back += 4) {
    const uint32_t before = image.Word(at - back);
    if (Opcode(before) == 15 && ((before >> 21) & 31) == RegisterA(word) && RegisterA(before) == 0) {
      return uint32_t(int32_t(Signed16(before)) * 65536 + Signed16(word));
    }
  }
  return std::nullopt;
}

struct Access {
  uint32_t at;
  uint32_t opcode;
  uint32_t address;
};

std::vector<Access> Accesses(const Image& image, uint32_t begin, uint32_t end) {
  std::vector<Access> out;
  for (uint32_t at = begin; at < end; at += 4) {
    if (const auto address = EffectiveAddress(image, at)) out.push_back({at, Opcode(image.Word(at)), *address});
  }
  return out;
}

bool Touches(const std::vector<Access>& accesses, uint32_t address, uint32_t opcode) {
  for (const Access& access : accesses) {
    if (access.address == address && access.opcode == opcode) return true;
  }
  return false;
}

constexpr uint32_t kLwz = 32;
constexpr uint32_t kStw = 36;
constexpr uint32_t kLbz = 34;
constexpr uint32_t kStb = 38;

std::optional<uint32_t> BranchTarget(const Image& image, uint32_t at) {
  const uint32_t word = image.Word(at);
  if (Opcode(word) == 16) return at + uint32_t(Signed16(word & 0xFFFC));
  if (Opcode(word) == 18) {
    int32_t offset = int32_t(word & 0x03FFFFFC);
    if (offset & 0x02000000) offset -= 0x04000000;
    return at + uint32_t(offset);
  }
  return std::nullopt;
}

bool IsUnconditionalForwardBranch(const Image& image, uint32_t at) {
  const uint32_t word = image.Word(at);
  const auto target = BranchTarget(image, at);
  return Opcode(word) == 18 && (word & 1) == 0 && target && *target > at;
}

bool IsLinkedCallTo(const Image& image, uint32_t at, uint32_t target) {
  const uint32_t word = image.Word(at);
  return Opcode(word) == 18 && (word & 1) == 1 && BranchTarget(image, at) == target;
}

// The step switch: after the state load, compare with 1 (blt, beq) and with 3 (blt) pick the block of each step.
struct StepBlocks {
  uint32_t step0 = 0;
  uint32_t step1 = 0;
  uint32_t step2 = 0;
  uint32_t step3 = 0;
};

std::optional<StepBlocks> FindStepBlocks(const Image& image, uint32_t begin, uint32_t end, uint32_t state) {
  for (const Access& access : Accesses(image, begin, end)) {
    if (access.address != state || access.opcode != kLwz) continue;
    uint32_t at = access.at;
    std::vector<uint32_t> compares;
    std::vector<uint32_t> branches;
    for (int i = 0; i < 14; ++i, at += 4) {
      const uint32_t word = image.Word(at);
      if (Opcode(word) == 10) compares.push_back(word & 0xFFFF);
      if (Opcode(word) == 16 || (Opcode(word) == 18 && (word & 1) == 0)) {
        if (const auto target = BranchTarget(image, at)) branches.push_back(*target);
      }
    }
    if (compares.size() == 3 && compares[0] == 1 && compares[1] == 1 && compares[2] == 3 && branches.size() >= 4) {
      return StepBlocks{branches[0], branches[1], branches[2], branches[3]};
    }
  }
  return std::nullopt;
}

std::vector<Access> BlockAccesses(const Image& image, uint32_t begin) {
  uint32_t end = begin;
  while (!IsUnconditionalForwardBranch(image, end)) end += 4;
  return Accesses(image, begin, end + 4);
}

uint32_t BlockEnd(const Image& image, uint32_t begin) {
  uint32_t end = begin;
  while (!IsUnconditionalForwardBranch(image, end)) end += 4;
  return end + 4;
}

std::vector<uint32_t> TableAddresses(const std::string& init) {
  std::vector<uint32_t> out;
  const std::regex entry(R"(\{ 0x([0-9A-F]{8}), \w+_sub_)");
  for (std::sregex_iterator it(init.begin(), init.end(), entry), end; it != end; ++it) {
    out.push_back(uint32_t(std::stoul((*it)[1], nullptr, 16)));
  }
  std::sort(out.begin(), out.end());
  return out;
}

uint32_t FunctionEnd(const std::vector<uint32_t>& table, uint32_t start) {
  const auto next = std::upper_bound(table.begin(), table.end(), start);
  return next == table.end() ? start + 0x800 : *next;
}

void TestHooksOfBothBuilds() {
  std::printf("skip intro hooks of both builds\n");
  CHECK(ssf2t::Base013SkipIntro::kXmlDocumentParse == 0x82416E80);
  CHECK(ssf2t::TitleUpdate023SkipIntro::kXmlDocumentParse == 0x82417548);
  CHECK(ssf2t::Base013SkipIntro::kBootLogoStep == 0x823ECB08);
  CHECK(ssf2t::TitleUpdate023SkipIntro::kBootLogoStep == 0x823ED1F8);

  const std::string parse_hooks = ReadSource("online_menu_hook.cpp");
  CHECK(Has(parse_hooks, "REX_HOOK_RAW(base013_sub_82416E80) { Base::ParseXml(ctx, base, __imp__base013_sub_82416E80); }"));
  CHECK(Has(parse_hooks, "REX_HOOK_RAW(tu023_sub_82417548) { Update::ParseXml(ctx, base, __imp__tu023_sub_82417548); }"));
  CHECK(Has(parse_hooks, "static_assert(ssf2t::Base013SkipIntro::kXmlDocumentParse == ssf2t::Base013PlayerMatch::kXmlDocumentParse);"));
  CHECK(Has(parse_hooks, "static_assert(ssf2t::TitleUpdate023SkipIntro::kXmlDocumentParse == "
                         "ssf2t::TitleUpdate023PlayerMatch::kXmlDocumentParse);"));

  const std::string hook = ReadSource("skip_intro_hook.cpp");
  CHECK(Has(hook, "REX_HOOK_RAW(base013_sub_823ECB08) {"));
  CHECK(Has(hook, "BootLogoStep<ssf2t::Base013SkipIntro>(ctx, base, __imp__base013_sub_823ECB08,"));
  CHECK(Has(hook, "{__imp__base013_sub_82173990, __imp__base013_sub_82173948}"));
  CHECK(Has(hook, "REX_HOOK_RAW(tu023_sub_823ED1F8) {"));
  CHECK(Has(hook, "BootLogoStep<ssf2t::TitleUpdate023SkipIntro>(ctx, base, __imp__tu023_sub_823ED1F8,"));
  CHECK(Has(hook, "{__imp__tu023_sub_821739B0, __imp__tu023_sub_82173968}"));
  CHECK(Has(hook, "#if SSF2T_TITLE_UPDATE_IMAGE"));
  CHECK(Has(ReadText(Root() / "CMakeLists.txt"), "src/skip_intro_hook.cpp"));

  const std::string parse = Between(parse_hooks, "static void ParseXml(PPCContext& ctx", "// Parses `xml` in place of");
  const size_t fsm_gate = parse.find("if (guest_text && REXCVAR_GET(ssf2t_skip_intro) && StartsLikeGameflowFsm(guest_text)) {");
  const size_t fsm_patch = parse.find("ParsePatched(ctx, base, original, *patched)");
  CHECK(fsm_gate != std::string::npos && fsm_patch > fsm_gate && fsm_patch < parse.rfind("original(ctx, base);"));

  for (const Build& build : kBuilds) {
    const std::string init = ReadText(Root() / "generated" / build.tree / "ssf2t_hd_remix_init.cpp");
    if (init.empty()) {
      std::printf("note: generated/%s is not on this machine, its function table is not checked\n", build.tree);
      continue;
    }
    char entry[96];
    for (const uint32_t address : {build.parse, build.step, build.ref_count, build.release}) {
      std::snprintf(entry, sizeof(entry), "{ 0x%08X, %s_sub_%08X }", address, build.hook_symbol, address);
      CHECK(Has(init, entry));
    }
  }
}

void TestBootLogoGate() {
  std::printf("skip intro boot logo gate\n");
  CHECK(ssf2t::kBootLogosEndStep == 2);
  CHECK(ssf2t::SkipsBootLogos(true, 0, 0));
  CHECK(ssf2t::SkipsBootLogos(true, 1, 0));
  CHECK(!ssf2t::SkipsBootLogos(true, 2, 0));
  CHECK(!ssf2t::SkipsBootLogos(true, 3, 0));
  CHECK(!ssf2t::SkipsBootLogos(true, 0, 1));
  CHECK(!ssf2t::SkipsBootLogos(false, 0, 0));
  CHECK(!ssf2t::SkipsBootLogos(false, 1, 0));

  const std::string hook = ReadSource("skip_intro_hook.cpp");
  const std::string step = Between(hook, "void BootLogoStep(PPCContext& ctx", "}  // namespace");
  const size_t gate = step.find("if (ssf2t::SkipsBootLogos(REXCVAR_GET(ssf2t_skip_intro), step, base[G::kBootLogoFinished])) {");
  const size_t release = step.find("ReleaseTexture(ctx, base, textures, G::kCapcomLogo);");
  const size_t write = step.find("ssf2t::StoreBE32(base, G::kBootLogoState, ssf2t::kBootLogosEndStep);");
  const size_t log = step.find("REXLOG_INFO(\"skip intro: startup presentation bypassed guest={}\", G::kName);");
  const size_t original = step.find("original(ctx, base);");
  CHECK(gate != std::string::npos && release > gate && write > release && log > write);
  CHECK(original != std::string::npos && original > log);
  CHECK(step.find("return", gate) == std::string::npos);
  CHECK(Count(hook, "REXLOG_INFO") == 1);
  CHECK(!Has(hook, "legal") && !Has(ReadSource("online_menu_hook.cpp"), "startup legal screen bypassed"));
}

void TestBootLogosInRealImages() {
  for (const Build& build : kBuilds) {
    Image image;
    const std::string init = ReadText(Root() / "generated" / build.tree / "ssf2t_hd_remix_init.cpp");
    if (!image.Load(build.image) || init.empty()) {
      std::printf("note: %s is not on this machine, the real boot logo code is not checked\n", build.image);
      continue;
    }
    std::printf("skip intro boot logos in the %s image\n", build.hook_symbol);
    const std::vector<uint32_t> table = TableAddresses(init);
    const uint32_t end = FunctionEnd(table, build.step);
    const std::vector<Access> all = Accesses(image, build.step, end);
    CHECK(Touches(all, build.state, kLwz));
    CHECK(Touches(all, build.state, kStw));
    CHECK(Touches(all, build.finished, kStb));
    CHECK(Touches(all, build.finished, kLbz));
    CHECK(Touches(all, build.capcom, kLwz) && Touches(all, build.capcom, kStw));
    CHECK(Touches(all, build.backbone, kLwz) && Touches(all, build.backbone, kStw));

    const auto blocks = FindStepBlocks(image, build.step, end, build.state);
    CHECK(blocks.has_value());
    if (!blocks) continue;
    const std::vector<Access> step0 = BlockAccesses(image, blocks->step0);
    const std::vector<Access> step1 = BlockAccesses(image, blocks->step1);
    const std::vector<Access> step2 = BlockAccesses(image, blocks->step2);
    const std::vector<Access> step3 = BlockAccesses(image, blocks->step3);
    CHECK(Touches(step0, build.capcom, kLwz) && !Touches(step0, build.backbone, kLwz));
    CHECK(Touches(step1, build.backbone, kLwz) && Touches(step1, build.capcom, kStw));
    CHECK(Touches(step2, build.backbone, kStw) && !Touches(step2, build.capcom, kLwz));
    CHECK(Touches(step2, build.finished, kStb));
    CHECK(Touches(step3, build.finished, kStb));

    bool releases_backbone = false;
    for (uint32_t at = blocks->step2; at < BlockEnd(image, blocks->step2); at += 4) {
      releases_backbone = releases_backbone || IsLinkedCallTo(image, at, build.release);
    }
    CHECK(releases_backbone);
    bool counts_and_releases_capcom = false;
    for (uint32_t at = blocks->step1; at < BlockEnd(image, blocks->step1); at += 4) {
      counts_and_releases_capcom = counts_and_releases_capcom || IsLinkedCallTo(image, at, build.ref_count);
    }
    CHECK(counts_and_releases_capcom);

  }
}

void TestBootLogosComeBeforeTheTitle() {
  for (const Build& build : kBuilds) {
    Image image;
    if (!image.Load(build.image)) {
      std::printf("note: %s is not on this machine, the boot order is not checked\n", build.image);
      continue;
    }
    std::printf("skip intro boot order in the %s image\n", build.hook_symbol);
    const std::string capcom_name = "backgrounds\\BGD_Capcom";
    const std::string backbone_name = "backgrounds\\BGD_Backbone";
    uint32_t capcom_string = 0;
    uint32_t backbone_string = 0;
    for (size_t at = 0; at + capcom_name.size() < image.bytes.size(); ++at) {
      if (image.bytes[at] != 'b') continue;
      const char* text = reinterpret_cast<const char*>(&image.bytes[at]);
      if (!capcom_string && std::strcmp(text, capcom_name.c_str()) == 0) capcom_string = kImageBase + uint32_t(at);
      if (!backbone_string && std::strcmp(text, backbone_name.c_str()) == 0) backbone_string = kImageBase + uint32_t(at);
    }
    CHECK(capcom_string != 0 && backbone_string != 0);

    const uint32_t scan_begin = build.step;
    uint32_t capcom_load = 0;
    uint32_t backbone_load = 0;
    for (uint32_t at = scan_begin; at < scan_begin + 0x1800; at += 4) {
      const uint32_t word = image.Word(at);
      if (Opcode(word) != 14) continue;
      for (uint32_t back = 4; back <= 16; back += 4) {
        const uint32_t before = image.Word(at - back);
        if (Opcode(before) == 15 && ((before >> 21) & 31) == RegisterA(word)) {
          const uint32_t address = uint32_t(int32_t(Signed16(before)) * 65536 + Signed16(word));
          if (address == capcom_string) capcom_load = at;
          if (address == backbone_string) backbone_load = at;
        }
      }
    }
    CHECK(capcom_load != 0 && backbone_load > capcom_load);
    if (!capcom_load || !backbone_load) continue;

    const auto stores_slot = [&](uint32_t from, uint32_t slot) {
      for (uint32_t at = from; at < from + 0x40; at += 4) {
        const auto address = EffectiveAddress(image, at);
        if (address && *address == slot && Opcode(image.Word(at)) == kStw) return true;
      }
      return false;
    };
    CHECK(stores_slot(capcom_load, build.capcom));
    CHECK(stores_slot(backbone_load, build.backbone));

    uint32_t game_flow_init = 0;
    uint32_t wait_for_finish = 0;
    for (uint32_t at = backbone_load; at < backbone_load + 0x400; at += 4) {
      if (!game_flow_init && IsLinkedCallTo(image, at, build.game_flow_init)) game_flow_init = at;
      const auto address = EffectiveAddress(image, at);
      if (address && *address == build.finished && Opcode(image.Word(at)) == kLbz) wait_for_finish = at;
    }
    CHECK(game_flow_init != 0 && wait_for_finish > game_flow_init);
  }
}

void TestNoSideEffects() {
  std::printf("skip intro presses nothing, jumps nowhere and writes nothing else\n");
  const std::string header = ReadSource("skip_intro.h");
  const std::string hook = ReadSource("skip_intro_hook.cpp");
  const std::string parse = ReadSource("online_menu_hook.cpp");
  const std::string skip_branch = Between(parse, "if (guest_text && REXCVAR_GET(ssf2t_skip_intro)", "original(ctx, base);");
  CHECK(!skip_branch.empty());
  for (const std::string* text : {&header, &hook, &skip_branch}) {
    CHECK(!Has(*text, "IsNewlyPressed") && !Has(*text, "title_to_main") && !Has(*text, "GameFlowSignal"));
    CHECK(!Has(*text, "SetControllerMask") && !Has(*text, "Input") && !Has(*text, "Button"));
    CHECK(!Has(*text, "Main_Menu"));
    CHECK(!Has(*text, "ofstream") && !Has(*text, "fopen") && !Has(*text, "XamContent") && !Has(*text, "Save"));
    CHECK(!Has(*text, "Sleep") && !Has(*text, "sleep") && !Has(*text, "timeout"));
  }
  CHECK(Count(hook, "StoreBE32(") == 2);
  CHECK(Count(hook, "ssf2t::StoreBE32(base, G::kBootLogoState, ssf2t::kBootLogosEndStep);") == 1);
  CHECK(!Has(hook, "kBootLogoFinished, 1") && !Has(hook, "base[G::kBootLogoFinished] ="));
  CHECK(!Has(header, "gameflow_fsm_Main_Menu") && !Has(header, "gameflow_fsm_background"));
  CHECK(Count(header, "replace(") == 1);
}

void TestStartupOnly() {
  std::printf("skip intro touches only the first boot of the game\n");
  std::ifstream in(Root() / "game/extracted/flog_x360.fpg", std::ios::binary);
  std::string document;
  if (in && ReadEntry(in, 453, 0xC05679BB, &document)) {
    const std::string fsm = StripCarriageReturns(document);
    CHECK(Count(fsm, "gameflow_fsm_LegalText_i000") == 3);
    const size_t boot_state = fsm.find("<State name=\"gameflow_fsm_load_screenset_i000\">");
    const size_t legal_use = fsm.find("dst=\"gameflow_fsm_LegalText_i000\"");
    CHECK(boot_state != std::string::npos && legal_use > boot_state);
    CHECK(fsm.find("dst=\"gameflow_fsm_LegalText_i000\"", legal_use + 1) == std::string::npos);
    CHECK(Count(fsm, "dst=\"gameflow_fsm_load_screenset_i000\"") == 1);
  } else {
    std::printf("note: game/extracted/flog_x360.fpg is not on this machine, the real document is not checked\n");
  }
  for (const Build& build : kBuilds) {
    Image image;
    if (!image.Load(build.image)) continue;
    uint32_t callers = 0;
    for (uint32_t at = kImageBase; at + 4 < kImageBase + uint32_t(image.bytes.size()) && callers < 8; at += 4) {
      if (IsLinkedCallTo(image, at, build.game_flow_init)) ++callers;
    }
    CHECK(callers == 1);
  }
}

}  // namespace

int main() {
  TestSetting();
  TestLauncherRow();
  TestDiagnostics();
  TestPatch();
  TestPatchFailsSafe();
  TestRealDocument();
  TestHooksOfBothBuilds();
  TestBootLogoGate();
  TestBootLogosInRealImages();
  TestBootLogosComeBeforeTheTitle();
  TestNoSideEffects();
  TestStartupOnly();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
