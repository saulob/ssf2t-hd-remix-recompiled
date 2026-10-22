#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "trg/file_dialog.h"

namespace {

using trg::dialog::Outcome;
using trg::dialog::Result;

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

std::string Response(const std::string& body) {
  return "/org/freedesktop/portal/desktop/request/1_42/trg1: org.freedesktop.portal.Request.Response " + body;
}

std::function<Result()> Backend(Outcome outcome, std::string path, int& calls) {
  return [outcome, path, &calls] {
    ++calls;
    return Result{outcome, path};
  };
}

}  // namespace

int main() {
  using namespace trg::dialog;

  Check(PathFromFileUri("file:///home/me/game.iso") == "/home/me/game.iso", "plain file URI");
  Check(PathFromFileUri("file://localhost/tmp/a") == "/tmp/a", "localhost host is local");
  Check(PathFromFileUri("file:///home/me/My%20Games/a%2Bb.iso") == "/home/me/My Games/a+b.iso", "percent-decoded spaces");
  Check(PathFromFileUri("file:///home/me/%C3%A9%E3%83%86.bin") == "/home/me/\xC3\xA9\xE3\x83\x86.bin", "UTF-8 bytes");
  Check(PathFromFileUri("file:///a%2").empty(), "truncated escape rejected");
  Check(PathFromFileUri("file:///a%zz").empty(), "bad escape rejected");
  Check(PathFromFileUri("file:///a%00b").empty(), "encoded NUL rejected");
  Check(PathFromFileUri("http://example.com/a").empty(), "non-file scheme rejected");
  Check(PathFromFileUri("file://server/share/a").empty(), "remote host rejected");
  Check(PathFromFileUri("file:relative").empty(), "relative URI rejected");

  Check(ParsePortalResponse(Response("(uint32 0, {'uris': <['file:///home/me/My%20Games/Pack']>})")).path ==
            "/home/me/My Games/Pack",
        "portal file reply");
  Check(ParsePortalResponse(Response("(uint32 0, {'uris': <['file:///home/me/Pack']>})")).outcome == Outcome::kSelected,
        "portal reply is a selection");
  Check(ParsePortalResponse(Response("(uint32 0, {'choices': <@a(ss) []>, 'uris': <['file:///a','file:///b']>})")).path == "/a",
        "first of several URIs");
  Check(ParsePortalResponse(Response("(uint32 1, @a{sv} {})")).outcome == Outcome::kCancelled, "portal cancel");
  Check(ParsePortalResponse(Response("(uint32 2, @a{sv} {})")).outcome == Outcome::kUnavailable, "portal failure");
  Check(ParsePortalResponse(Response("(uint32 0, {'uris': <['https://x/a']>})")).outcome == Outcome::kUnavailable,
        "non-file portal URI is not a selection");
  Check(ParsePortalResponse(Response("(uint32 0, @a{sv} {})")).outcome == Outcome::kUnavailable, "reply without URIs");
  Check(ParsePortalResponse("").outcome == Outcome::kUnavailable, "empty reply");

  const std::string file_opts = PortalOptions("tok", {{"Packages", "*.pkg;*.bin"}}, false);
  Check(file_opts.find("'handle_token': <'tok'>") != std::string::npos, "options carry the handle token");
  Check(file_opts.find("('Packages', [(0, '*.pkg'), (0, '*.bin')])") != std::string::npos, "filter patterns split");
  Check(file_opts.find("('All files', [(0, '*')])") != std::string::npos, "All files filter appended");
  Check(file_opts.find("directory") == std::string::npos, "file options are not a folder pick");
  Check(PortalOptions("tok", {{"It's", "*"}}, false).find("'It\\'s'") != std::string::npos, "quotes escaped");
  const std::string folder_opts = PortalOptions("tok", {}, true);
  Check(folder_opts.find("'directory': <true>") != std::string::npos, "folder options");
  Check(folder_opts.find("filters") == std::string::npos, "folder options have no filters");

  int portal = 0, zenity = 0, kdialog = 0;
  Result r = RunBackends({Backend(Outcome::kSelected, "/p", portal), Backend(Outcome::kSelected, "/z", zenity),
                          Backend(Outcome::kSelected, "/k", kdialog)});
  Check(r.outcome == Outcome::kSelected && r.path == "/p" && !zenity && !kdialog, "portal success stops the chain");

  portal = zenity = kdialog = 0;
  r = RunBackends({Backend(Outcome::kCancelled, "", portal), Backend(Outcome::kSelected, "/z", zenity),
                   Backend(Outcome::kSelected, "/k", kdialog)});
  Check(r.outcome == Outcome::kCancelled && !zenity && !kdialog, "portal cancel does not open another picker");

  portal = zenity = kdialog = 0;
  r = RunBackends({Backend(Outcome::kUnavailable, "", portal), Backend(Outcome::kSelected, "/z", zenity),
                   Backend(Outcome::kSelected, "/k", kdialog)});
  Check(r.outcome == Outcome::kSelected && r.path == "/z" && !kdialog, "portal unavailable falls back to zenity");

  portal = zenity = kdialog = 0;
  r = RunBackends({Backend(Outcome::kUnavailable, "", portal), Backend(Outcome::kUnavailable, "", zenity),
                   Backend(Outcome::kSelected, "/k", kdialog)});
  Check(r.outcome == Outcome::kSelected && r.path == "/k", "zenity unavailable falls back to kdialog");

  portal = zenity = kdialog = 0;
  r = RunBackends({Backend(Outcome::kUnavailable, "", portal), Backend(Outcome::kCancelled, "", zenity),
                   Backend(Outcome::kSelected, "/k", kdialog)});
  Check(r.outcome == Outcome::kCancelled && !kdialog, "zenity cancel does not open kdialog");

  portal = zenity = kdialog = 0;
  r = RunBackends({Backend(Outcome::kUnavailable, "", portal), Backend(Outcome::kUnavailable, "", zenity),
                   Backend(Outcome::kUnavailable, "", kdialog)});
  Check(r.outcome == Outcome::kUnavailable && r.path.empty(), "no backend is reported unavailable");
  Check(RunBackends({}).outcome == Outcome::kUnavailable, "empty chain is unavailable");

  std::puts(failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
