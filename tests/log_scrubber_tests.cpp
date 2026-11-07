// The Bug Report log privacy scrubber: what it removes, what it must keep, and how it treats bytes. Headless.
// Run: ssf2t_log_scrubber_tests

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#include "log_scrubber.h"
#include "online/identity.h"

using namespace ssf2t;
namespace fs = std::filesystem;

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

bool Has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

const std::string kToken(64, 'a');
const std::string kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

PrivateFacts Facts() {
  PrivateFacts facts;
  facts.user_names = {"SauloB"};
  facts.machine_names = {"DESKTOP-ABC123"};
  facts.profile_folders = {"C:\\Users\\SauloB"};
  facts.install_folders = {"S:\\Games\\SSF2T HD Remix"};
  facts.secrets = {kToken, kHash};
  facts.own_codes = {"YYV7-PP2Z"};
  facts.player_names = {"Ryu_Fan"};
  return facts;
}

std::string Scrub(const std::string& text) { return ScrubText(text, Facts()); }
std::string ScrubAnonymous(const std::string& text) { return ScrubText(text, PrivateFacts{}); }

void TestUserAndComputerNames() {
  const std::string out = Scrub("Hello SauloB on DESKTOP-ABC123, welcome");
  CHECK(Has(out, "<user>") && Has(out, "<computer>"));
  CHECK(!Has(out, "SauloB") && !Has(out, "DESKTOP-ABC123"));
  CHECK(Has(Scrub("hello saulob"), "<user>"));
  CHECK(Scrub("Admin menu opened") == "Admin menu opened");
  CHECK(Scrub("SauloBX stays") == "SauloBX stays");
  const std::string name = Scrub("online signed in name=Ryu_Fan");
  CHECK(Has(name, "name=<name>") && !Has(name, "Ryu_Fan"));
  CHECK(Scrub("online signed in name=Player") == "online signed in name=Player");
}

void TestWindowsProfilePaths() {
  const std::string known = Scrub("Loading C:\\Users\\SauloB\\AppData\\Roaming\\ssf2t\\settings.toml");
  CHECK(Has(known, "<home>\\AppData\\Roaming\\ssf2t\\settings.toml") && !Has(known, "SauloB"));
  const std::string other = ScrubAnonymous("Opening C:\\Users\\OtherPerson\\Documents\\save.dat");
  CHECK(Has(other, "<home>\\Documents\\save.dat") && !Has(other, "OtherPerson"));
  const std::string slashes = ScrubAnonymous("path d:/users/Maria Souza/Desktop/x.log");
  CHECK(Has(slashes, "<home>") && !Has(slashes, "Maria"));
  const std::string doubled = Scrub("\"path\":\"C:\\\\Users\\\\SauloB\\\\ssf2t\\\\x.ini\"");
  CHECK(!Has(doubled, "SauloB") && Has(doubled, "<home>"));
}

void TestPosixHomePaths() {
  CHECK(ScrubAnonymous("/home/ana/.local/share/ssf2t/save.dat") == "<home>/.local/share/ssf2t/save.dat");
  CHECK(!Has(ScrubAnonymous("cache in /Users/bob/Library/ssf2t"), "bob"));
  CHECK(!Has(ScrubAnonymous("mounted /run/media/carol/disk/ssf2t"), "carol"));
  CHECK(ScrubAnonymous("loaded /usr/lib/libssl.so.3") == "loaded /usr/lib/libssl.so.3");
  PrivateFacts facts;
  facts.profile_folders = {"/home/deck"};
  CHECK(ScrubText("data in /home/deck/ssf2t/logs", facts) == "data in <home>/ssf2t/logs");
}

void TestInstallAndOtherFolders() {
  const std::string install = Scrub("Game files: S:\\Games\\SSF2T HD Remix\\game\\default.xex valid");
  CHECK(Has(install, "<install>\\game\\default.xex") && !Has(install, "Games"));
  const std::string folder = ScrubAnonymous("Reading D:\\Stuff\\Secret Folder\\file.txt");
  CHECK(Has(folder, "<folder>\\file.txt") && !Has(folder, "Secret") && !Has(folder, "Stuff"));
  const std::string system = ScrubAnonymous("Module C:\\Windows\\System32\\kernel32.dll loaded");
  CHECK(system == "Module C:\\Windows\\System32\\kernel32.dll loaded");
  const std::string posix = ScrubAnonymous("mount /mnt/photos-of-me/ssf2t/game");
  CHECK(!Has(posix, "photos-of-me"));
}

void TestNetworkSharePaths() {
  const std::string share = ScrubAnonymous("Opening \\\\fileserver\\share\\team\\save.dat now");
  CHECK(Has(share, "<network-path>") && !Has(share, "fileserver") && !Has(share, "team"));
  const std::string host_only = ScrubAnonymous("Connecting to \\\\nas01 failed");
  CHECK(Has(host_only, "<network-path>") && !Has(host_only, "nas01"));
  const std::string local = ScrubAnonymous("Opening \\\\?\\C:\\Windows\\x.dll");
  CHECK(!Has(local, "<network-path>"));
}

void TestAddresses() {
  const std::string v4 = ScrubAnonymous("peer 203.0.113.9:7001 raw_address=198.51.100.4 lan 192.168.1.20");
  CHECK(!Has(v4, "203.0.113.9") && !Has(v4, "198.51.100.4") && !Has(v4, "192.168.1.20"));
  CHECK(Has(v4, "<ipv4>:7001"));
  const std::string local = ScrubAnonymous("bound 0.0.0.0:7001 and 127.0.0.1");
  CHECK(local == "bound 0.0.0.0:7001 and 127.0.0.1");
  const std::string v6 = ScrubAnonymous("client 2001:db8:85a3::8a2e:370:7334 connected, loopback ::1");
  CHECK(!Has(v6, "2001:db8") && Has(v6, "<ipv6>") && Has(v6, "::1"));
  const std::string zoned = ScrubAnonymous("link fe80::1ff:fe23:4567:890a%eth0 up");
  CHECK(!Has(zoned, "fe80") && Has(zoned, "<ipv6>"));
  CHECK(Has(ScrubAnonymous("STUN stun.example: 191.254.13.12:7001"), "<ipv4>:7001"));
}

void TestVersionsAndTimestampsStay() {
  for (const char* line : {"Game files: valid (default.xex 0.0.1.3, CRC32 B734F075; default.xexp to 0.0.2.3, CRC32 38DE2310)",
                           "NVIDIA driver 551.23.0.1 installed", "Vulkan 1.3.275.0 loaded", "kernel 5.15.153.1 on Linux",
                           "Build: RelWithDebInfo x86-64 | ReXGlue SDK 0.10.0 | Direct3D 12", "game version 0.0.2.3 (Title Update #1)",
                           "Super Street Fighter II Turbo HD Remix port 0.4.0",
                           "[2026-11-06 23:27:24.394] [info] [core] [t31504] online worker connected",
                           "Guest memory arena mapped: virtual base 0x0000000100000000"}) {
    CHECK(ScrubAnonymous(line) == line);
  }
}

void TestSecrets() {
  const std::string authorization = ScrubAnonymous("Authorization: Bearer abc.def.ghi");
  CHECK(Has(authorization, "<secret>") && !Has(authorization, "abc.def"));
  const std::string bearer = ScrubAnonymous("sent bearer xyz123token to the service");
  CHECK(Has(bearer, "bearer <secret>") && !Has(bearer, "xyz123"));
  CHECK(Has(ScrubAnonymous("token=abc123 next"), "token=<secret> next"));
  CHECK(Has(ScrubAnonymous("{\"relay_token\": 4021337711}"), "<secret>"));
  CHECK(!Has(ScrubAnonymous("relay_token=1234567"), "1234567"));
  CHECK(!Has(ScrubAnonymous("receipt: 77aa88bb"), "77aa88bb"));
  CHECK(!Has(ScrubAnonymous("password = hunter2!"), "hunter2"));
  CHECK(Has(ScrubAnonymous("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.signaturepart"), "<secret>"));

  const std::string known = Scrub("hello token " + kToken + " hash " + kHash);
  CHECK(!Has(known, kToken) && !Has(known, kHash) && Has(known, "<secret>"));
  const std::string unknown = ScrubAnonymous("receipt " + std::string(64, 'c') + " stored");
  CHECK(!Has(unknown, "cccccccc") && Has(unknown, "<secret>"));
  CHECK(ScrubAnonymous("short hex deadbeef stays") == "short hex deadbeef stays");
}

void TestFriendCodesAndIdentifiers() {
  const std::string codes = Scrub("online friend request sent code=ABCD-2345 from=YYV7-PP2Z");
  CHECK(Has(codes, "code=<friend-code>") && Has(codes, "from=<own-code>"));
  CHECK(!Has(codes, "ABCD-2345") && !Has(codes, "YYV7-PP2Z"));
  CHECK(ScrubAnonymous("range 2026-11-06 and CRC32 38DE2310") == "range 2026-11-06 and CRC32 38DE2310");
  CHECK(ScrubAnonymous("abcd-2345 lowercase stays") == "abcd-2345 lowercase stays");
  CHECK(Has(ScrubAnonymous("x ABCD-2345-EFGH y"), "ABCD-2345-EFGH"));

  const std::string ids = ScrubAnonymous("relay available match=3f9a1c2b7d4e5f60 session=abcdef0123456789 invite=0011223344556677");
  CHECK(Has(ids, "match=<match>") && Has(ids, "session=<session>") && Has(ids, "invite=<invite>"));
  CHECK(!Has(ids, "3f9a1c2b") && !Has(ids, "abcdef0123"));
  CHECK(ScrubAnonymous("peers match=12 left") == "peers match=12 left");
  CHECK(ScrubAnonymous("player_match=3f9a1c2b7d4e5f60") == "player_match=3f9a1c2b7d4e5f60");
}

void TestCharacterHandling() {
  CHECK(ScrubAnonymous("Configura\xC3\xA7\xC3\xA3o do jogo") == "Configura\xC3\xA7\xC3\xA3o do jogo");
  CHECK(ScrubAnonymous("a\x01" "b\x07" "c\r\nd\te") == "abc\nd\te");
  CHECK(ScrubAnonymous("one\r\ntwo\r\n") == "one\ntwo\n");
  CHECK(ScrubAnonymous("bad\xFF" "byte") == "bad?byte");
  CHECK(ScrubAnonymous("cut \xE2\x82") == "cut ??");
  CHECK(ScrubAnonymous("c1\xC2\x85" "gone") == "c1gone");
  CHECK(ScrubAnonymous("overlong \xC0\xAF") == "overlong ??");
  CHECK(ScrubAnonymous(std::string("nul\0inside", 10)) == "nulinside");
  const std::string name = ScrubText("Ol\xC3\xA1 Jo\xC3\xA3o, ol\xC3\xA1", [] {
    PrivateFacts facts;
    facts.user_names = {"Jo\xC3\xA3o"};
    return facts;
  }());
  CHECK(Has(name, "<user>") && !Has(name, "Jo\xC3\xA3o") && Has(name, "Ol\xC3\xA1"));
  CHECK(ScrubAnonymous("") == "");
}

void TestOrdinaryLinesStay() {
  const std::string lines =
      "[2026-11-06 23:27:23.620] [info] [core] [t31504] netplay fight phase=Fighting was=Intro generation=3 fight=2 guest=tu023\n"
      "[2026-11-06 23:27:24.528] [info] [core] [t31504] online service capabilities protocol=3 worker_build=2026-11-07-bug-reports\n"
      "Direct3D 12 device and OS features:\n"
      "* Max GPU virtual address bits per resource: 40\n";
  CHECK(ScrubAnonymous(lines) == lines);
}

void TestGatherUsesTheOnlineIdentity() {
  const fs::path dir = fs::temp_directory_path() / "ssf2t_log_scrubber_identity";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const PrivateFacts none = GatherPrivateFacts({}, dir, "Ryu_Fan");
  CHECK(none.secrets.empty() && none.own_codes.empty());
  CHECK(!fs::exists(dir / online::kIdentityFileName));
  CHECK(!none.player_names.empty() && none.player_names.front() == "Ryu_Fan");

  const online::Identity identity = online::LoadOrCreateIdentity(dir);
  CHECK(identity.ok());
  const PrivateFacts facts = GatherPrivateFacts({fs::path("S:\\Games\\SSF2T")}, dir, "Ryu_Fan");
  CHECK(facts.secrets.size() == 2 && facts.secrets[0] == identity.token && facts.secrets[1].size() == 64);
  CHECK(facts.own_codes.size() == 1 && facts.own_codes[0] == identity.player_id);
  const std::string line = "signed in token=" + identity.token + " hash " + facts.secrets[1] + " code=" + identity.player_id +
                           " dir " + dir.string();
  const std::string out = ScrubText(line, facts);
  CHECK(!Has(out, identity.token) && !Has(out, facts.secrets[1]) && !Has(out, identity.player_id));
  CHECK(Has(out, "<own-code>") && Has(out, "<install>"));
  fs::remove_all(dir);
}

}  // namespace

int main() {
  TestUserAndComputerNames();
  TestWindowsProfilePaths();
  TestPosixHomePaths();
  TestInstallAndOtherFolders();
  TestNetworkSharePaths();
  TestAddresses();
  TestVersionsAndTimestampsStay();
  TestSecrets();
  TestFriendCodesAndIdentifiers();
  TestCharacterHandling();
  TestOrdinaryLinesStay();
  TestGatherUsesTheOnlineIdentity();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
