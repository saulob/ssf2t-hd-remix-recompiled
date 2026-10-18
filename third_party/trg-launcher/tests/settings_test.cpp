// TextFileSettings must keep a hand-written settings file intact: comments,
// order and spacing, editing values in place.
#include <cstdio>
#include <string>

#include "trg/platform.h"
#include "trg/settings.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

std::string ReadAll(const char* path) {
  std::string s;
  if (FILE* f = trg::OpenFile(path, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
  }
  return s;
}

}  // namespace

int main() {
  const char* path = "trg_settings_test.txt";
  if (FILE* f = trg::OpenFile(path, "wb")) {
    fputs("# Game settings\r\nvsync = off  # tearing\n# frame_rate = 30\n\nwidescreen = off\n", f);
    fclose(f);
  }

  trg::TextFileSettings s(path);
  Check(s.Get("vsync") == "off", "reads a value, ignoring its trailing comment");
  Check(s.Get("frame_rate", "30") == "30", "commented-out setting falls back to the default");
  Check(!s.GetBool("vsync", true), "GetBool reads off");
  Check(s.GetInt("missing", 7) == 7, "GetInt default");

  s.Set("vsync", "on");
  s.Set("frame_rate", "60");
  s.Set("new_key", "1");
  Check(s.Save(), "saves");
  Check(ReadAll(path) ==
            "# Game settings\nvsync = on\nframe_rate = 60\n\nwidescreen = off\n\n# Set from the launcher\nnew_key = 1\n",
        "edits in place, fills the commented example, appends new keys");

  trg::TextFileSettings again(path);
  Check(again.Get("frame_rate") == "60" && again.GetBool("new_key", false), "reloads what it wrote");

  again.ResetToDefaults();
  Check(again.Save(), "saves after reset");
  Check(ReadAll(path) == "# Game settings\n# vsync = on\n# frame_rate = 60\n\n# widescreen = off\n\n# Set from the "
                         "launcher\n# new_key = 1\n",
        "reset comments every setting out");

  trg::MemorySettings m;
  m.true_value = "true";
  m.false_value = "false";
  m.SetBool("x", true);
  Check(m.Get("x") == "true" && m.GetBool("x", false), "custom true/false values");

  trg::RemoveFile(path);
  std::printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
