// Static checks that the recompiled game images ship in the one executable and that the build keeps them current:
// codegen runs before CMake and Ninja (never inside them, so a precompiled header cannot go stale under a running
// build), a regenerated image reconfigures the build and recompiles, the base game alone is a complete build, the
// Title Update image is optional, and no launcher or runtime text talks about builds or code generation.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifndef SSF2T_SOURCE_DIR
#error SSF2T_SOURCE_DIR must name the repository root
#endif

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

std::string Read(const char* relative) {
  std::ifstream in(std::string(SSF2T_SOURCE_DIR) + "/" + relative, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  std::string out = text.str();
  std::erase(out, '\r');
  return out;
}

bool Has(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

size_t Count(const std::string& text, const std::string& part) {
  size_t count = 0;
  for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) {
    ++count;
  }
  return count;
}

void TestImagesAreCompiledIntoTheOneExecutable() {
  const std::string module = Read("cmake/ssf2t_guest_images.cmake");
  const std::string cmake = Read("CMakeLists.txt");
  CHECK(Has(module, "ssf2t_add_guest_image(ssf2t_guest_base013 base)"));
  CHECK(Has(module, "ssf2t_add_guest_image(ssf2t_guest_tu023 tu)"));
  CHECK(Has(cmake, "target_link_libraries(ssf2t_hd_remix PRIVATE rex::runtime ${SSF2T_GUEST_IMAGES})"));
  CHECK(Count(cmake, "add_executable(ssf2t_hd_remix WIN32") == 1);
  CHECK(Count(cmake, "add_executable(ssf2t_hd_remix ${SSF2T_SOURCES}") == 1);
  CHECK(!Has(cmake, "add_executable(ssf2t_hd_remix_"));
  CHECK(Has(cmake, "src/guest_image.cpp"));

  const std::string image = Read("src/guest_image.cpp");
  CHECK(Has(image, "extern const rex::PPCImageInfo base013_PPCImageConfig;"));
  CHECK(Has(image, "extern const rex::PPCImageInfo tu023_PPCImageConfig;"));
  CHECK(Has(image, "return tu023_PPCImageConfig;"));
  CHECK(Has(image, "return base013_PPCImageConfig;"));

  // The host never includes a generated header: that would put both images' macros in one translation unit.
  CHECK(!Has(Read("src/main.cpp"), "generated/"));
  CHECK(Has(Read("src/guest_image.cpp"), "#if SSF2T_TITLE_UPDATE_IMAGE\nextern const rex::PPCImageInfo tu023_PPCImageConfig;"));
  CHECK(!Has(Read("src/ssf2t_hd_remix_app.h"), "generated/"));
}

void TestGeneratedHeaderChangesInvalidateThePrecompiledHeader() {
  const std::string module = Read("cmake/ssf2t_guest_images.cmake");
  CHECK(Has(module, "set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS \"${_stamp}\")"));
  CHECK(Has(module, "OBJECT_DEPENDS \"${_stamp}\""));
  CHECK(Has(module, "target_precompile_headers(${name} PRIVATE \"${_dir}/ssf2t_hd_remix_pch.h\")"));
  CHECK(Has(module, "set(_stamp \"${_dir}/ssf2t-guest.stamp\")"));

  // Codegen is not a build step: nothing here may rewrite generated files while Ninja is running.
  for (const char* codegen : {"rex::rexglue", "rexglue codegen", "add_custom_command"}) {
    CHECK(!Has(module, codegen));
  }
  CHECK(!Has(Read("CMakeLists.txt"), "rex::rexglue"));

  // Stale images fail before anything compiles.
  CHECK(Has(module, "add_custom_target(ssf2t_guest_check COMMAND ${_check} VERBATIM)"));
  CHECK(Has(module, "tools/codegen.ps1\" -Check"));
  CHECK(Has(module, "tools/check-guest-code.sh"));
  CHECK(Has(module, "foreach(_image IN LISTS SSF2T_GUEST_IMAGES)\n        add_dependencies(${_image} ssf2t_guest_check)"));

  // build.ps1 refreshes the images before CMake configures or builds.
  const std::string build = Read("tools/build.ps1");
  const size_t codegen = build.find("& $GuestCodegen");
  const size_t configure = build.find("& $CMake -S $Root");
  const size_t compile = build.find("& $CMake --build");
  CHECK(codegen != std::string::npos && configure != std::string::npos && compile != std::string::npos);
  CHECK(codegen < configure && configure < compile);
}

void TestBaseGameAloneIsACompleteBuild() {
  const std::string build = Read("tools/build.ps1");
  CHECK(!Has(build, "Missing $Xexp"));
  CHECK(!Has(build, "throw \"Unsupported $Xexp"));
  CHECK(Has(build, "$TitleUpdateImage = Test-Path (Join-Path $Root 'generated\\tu\\ssf2t-guest.stamp')"));
  CHECK(Has(build, "$TitleUpdateSetting = if ($TitleUpdateImage) { 'ON' } else { 'OFF' }"));
  CHECK(Has(build, "\"-DSSF2T_TITLE_UPDATE_IMAGE=$TitleUpdateSetting\""));
  CHECK(Has(build, "SSF2T_TITLE_UPDATE_IMAGE:STRING=$TitleUpdateSetting"));
  // Only a universal build asks for the Title Update image.
  CHECK(Has(build, "if ($RequireTitleUpdate -and -not $TitleUpdateImage) { throw"));
  CHECK(Has(Read("tools/package-release.ps1"), "RequireTitleUpdate = $true"));
  CHECK(Has(Read("tools/package-linux.sh"), "-DSSF2T_TITLE_UPDATE_IMAGE=ON"));

  const std::string module = Read("cmake/ssf2t_guest_images.cmake");
  CHECK(Has(module, "set(SSF2T_GUEST_IMAGES ssf2t_guest_base013)\nssf2t_add_guest_image(ssf2t_guest_base013 base)"));
  CHECK(Has(module, "SSF2T_TITLE_UPDATE_IMAGE STREQUAL \"AUTO\" AND EXISTS"));
  CHECK(Has(module, "target_compile_definitions(${host} PRIVATE SSF2T_TITLE_UPDATE_IMAGE=${SSF2T_TITLE_UPDATE_IMAGE_BUILT})"));

  const std::string codegen = Read("tools/codegen.ps1");
  CHECK(Has(codegen, "Title Update not present: building base game support only (0.0.1.3)"));
  CHECK(Has(codegen, "Find-TitleUpdateSource (Join-Path $root 'game\\extracted') $searchRoot $tuTemp"));
  CHECK(Has(codegen, "Copy-Item -LiteralPath $PatchSource -Destination $stagedPatch -Force"));
  CHECK(Has(Read("tools/guest-code.ps1"), "StfsExtract]::ContentType($file.FullName) -ne 0x000B0000"));
  CHECK(Has(codegen, "Test-GuestCurrent $item"));
  CHECK(Has(codegen, "Assert-GuestStaging $Item"));
  CHECK(Has(codegen, "Write-GuestManifest $Item"));
  CHECK(Has(codegen, "$ErrorActionPreference = 'Stop'"));
  // game\extracted is only read: nothing in codegen deletes or renames inside it.
  CHECK(!Has(codegen, "Remove-Item -LiteralPath $xex"));
  CHECK(!Has(codegen, "Remove-Item -LiteralPath $xexp"));
  CHECK(!Has(codegen, "Move-Item -LiteralPath $xex"));

  // Without the Title Update image, the hooks and image table of that version are not compiled.
  for (const char* hook : {"src/fight_area_hook.cpp", "src/achievement_ui_hook.cpp", "src/menu_text_hook.cpp", "src/infinite_time.cpp"}) {
    const std::string text = Read(hook);
    const size_t guard = text.find("#if SSF2T_TITLE_UPDATE_IMAGE");
    const size_t tu = text.find("REX_HOOK_RAW(tu023_");
    CHECK(guard != std::string::npos && tu != std::string::npos && guard < tu);
    CHECK(text.find("base013_") < guard);
  }
  // File validity never depends on what the program was built with.
  CHECK(!Has(Read("src/game_files.cpp"), "|| !kTitleUpdateImageBuilt"));
  CHECK(Has(Read("src/game_files.cpp"), "check.guest == GuestVersion::kTitleUpdate023 && !host.title_update"));
  // The currency check reads config files only, never the game files.
  const std::string library = Read("tools/guest-code.ps1");
  const size_t stamp = library.find("function Get-GuestStamp");
  const size_t current = library.find("function Test-GuestCurrent");
  CHECK(stamp != std::string::npos && current != std::string::npos);
  const std::string reads = library.substr(stamp, library.find("function Add-GuestSymbolPrefix") - stamp);
  CHECK(!Has(reads, "game\\extracted"));
  CHECK(Has(library, "xexp=$(if ($Variant.Update) { $GuestXexpSha256.ToLowerInvariant() } else { 'none' })"));

  // Each image has its own overrides and function list; the base ones are the pre-Title-Update analysis.
  CHECK(Has(Read("config/guest/base/overrides.toml"), "0x824521C0 = {}"));
  CHECK(Has(Read("config/guest/base/discovered-functions.toml"), "0x839D3020 = { end = 0x839D3030 }"));
  CHECK(Has(Read("config/guest/base/discovered-functions.toml"), "0x839DAFE8 = { end = 0x839DAFF0 }"));
  CHECK(Has(Read("config/guest/tu/overrides.toml"), "0x82452D00 = {}"));
  // ReXGlue resolves relative manifest paths against the manifest's folder, so the templates name paths absolutely.
  CHECK(Has(Read("config/guest/base/manifest.template.toml"), "file_path = \"@ROOT@/out/codegen/base/input/default.xex\""));
  CHECK(Has(Read("config/guest/tu/manifest.template.toml"), "file_path = \"@ROOT@/out/codegen/tu/input/default.xex\""));
  CHECK(Has(Read("config/guest/base/manifest.template.toml"), "out_directory_path = \"@ROOT@/out/codegen/base/raw\""));
}

void TestNoDeveloperWordingForPlayers() {
  const char* sources[] = {"src/launcher.cpp", "src/game_files.cpp", "src/ssf2t_hd_remix_app.h", "src/launcher.h"};
  const char* forbidden[] = {"run codegen", "run build", "recompile the", "recompile it", "SSF2T_GAME_BASE", "Build is for",
                             "guest version mismatch", "build mismatch", "generated for", "rebuild"};
  for (const char* source : sources) {
    const std::string text = Read(source);
    CHECK(!text.empty());
    for (const char* word : forbidden) {
      if (Has(text, word)) std::printf("  %s mentions \"%s\"\n", source, word);
      CHECK(!Has(text, word));
    }
  }
  // Nothing says every install needs the Title Update.
  CHECK(!Has(Read("src/ssf2t_hd_remix_app.h"), "this port needs version"));
  CHECK(!Has(Read("src/game_files.cpp"), "Title Update needed"));
  CHECK(!Has(Read("src/game_files.cpp"), "Title Update #1 needed"));
}

}  // namespace

int main() {
  TestImagesAreCompiledIntoTheOneExecutable();
  TestGeneratedHeaderChangesInvalidateThePrecompiledHeader();
  TestBaseGameAloneIsACompleteBuild();
  TestNoDeveloperWordingForPlayers();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
