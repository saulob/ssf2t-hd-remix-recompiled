// Online identity persistence, the temporary identity of a second instance, and the instance lock. Headless.
// Run: ssf2t_online_identity_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "online/identity.h"
#include "online/instance.h"

using namespace ssf2t::online;
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

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

fs::path FreshDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

void TestIdentityIsCreatedOnceAndKept() {
  const fs::path dir = FreshDir("ssf2t_online_identity_a");
  const Identity first = LoadOrCreateIdentity(dir);
  CHECK(first.ok() && !first.ephemeral && first.token.size() == 64 && first.player_id.size() == 9);
  CHECK(first.player_id == DerivePlayerId(first.token));
  const std::string file = ReadFile(dir / kIdentityFileName);
  const Identity second = LoadOrCreateIdentity(dir);
  CHECK(second.ok() && second.token == first.token && second.player_id == first.player_id);
  CHECK(ReadFile(dir / kIdentityFileName) == file);
  CHECK(!fs::exists(dir / (std::string(kIdentityFileName) + ".new")));
  fs::remove_all(dir);
}

void TestExistingIdentityIsReadWithoutCreatingOne() {
  const fs::path dir = FreshDir("ssf2t_online_identity_existing");
  const Identity none = LoadExistingIdentity(dir);
  CHECK(!none.ok() && !none.error.empty());
  CHECK(!fs::exists(dir / kIdentityFileName));
  const Identity made = LoadOrCreateIdentity(dir);
  const Identity read = LoadExistingIdentity(dir);
  CHECK(read.ok() && read.token == made.token && read.player_id == made.player_id);
  std::ofstream(dir / kIdentityFileName, std::ios::binary | std::ios::trunc) << "token=nothex\n";
  CHECK(!LoadExistingIdentity(dir).ok());
  CHECK(ReadFile(dir / kIdentityFileName) == "token=nothex\n");
  fs::remove_all(dir);
}

void TestDamagedIdentityIsReportedNotReplaced() {
  const fs::path dir = FreshDir("ssf2t_online_identity_b");
  std::ofstream(dir / kIdentityFileName, std::ios::binary) << "token=nothex\n";
  const Identity id = LoadOrCreateIdentity(dir);
  CHECK(!id.ok() && !id.error.empty());
  CHECK(ReadFile(dir / kIdentityFileName) == "token=nothex\n");
  fs::remove_all(dir);
}

void TestEphemeralIdentityIsNeverWritten() {
  const fs::path dir = FreshDir("ssf2t_online_identity_c");
  const Identity main_identity = LoadOrCreateIdentity(dir);
  const std::string file = ReadFile(dir / kIdentityFileName);
  const Identity a = MakeEphemeralIdentity();
  const Identity b = MakeEphemeralIdentity();
  CHECK(a.ok() && a.ephemeral && b.ok());
  CHECK(a.token != b.token && a.player_id != b.player_id);
  CHECK(a.token != main_identity.token && a.player_id != main_identity.player_id);
  CHECK(ReadFile(dir / kIdentityFileName) == file);
  fs::remove_all(dir);
}

void TestSecondInstanceCannotTakeTheLock() {
  const fs::path dir = FreshDir("ssf2t_online_identity_d");
  auto primary = InstanceLock::TryAcquire(dir);
  CHECK(primary != nullptr);
  CHECK(InstanceLock::TryAcquire(dir) == nullptr);
  const fs::path other = FreshDir("ssf2t_online_identity_e");
  auto elsewhere = InstanceLock::TryAcquire(other);
  CHECK(elsewhere != nullptr);
  primary.reset();
  auto again = InstanceLock::TryAcquire(dir);
  CHECK(again != nullptr);
  again.reset();
  elsewhere.reset();
  fs::remove_all(dir);
  fs::remove_all(other);
}

void TestTwoInstanceFoldersHaveTheirOwnIdentityAndFriendCode() {
  const fs::path a_dir = FreshDir("ssf2t_online_identity_f");
  const fs::path b_dir = FreshDir("ssf2t_online_identity_g");
  auto a_lock = InstanceLock::TryAcquire(a_dir);
  auto b_lock = InstanceLock::TryAcquire(b_dir);
  CHECK(a_lock && b_lock);
  const Identity a = LoadOrCreateIdentity(a_dir);
  const Identity b = LoadOrCreateIdentity(b_dir);
  CHECK(a.ok() && b.ok() && !a.ephemeral && !b.ephemeral);
  CHECK(a.token != b.token && a.player_id != b.player_id);
  CHECK(LoadOrCreateIdentity(a_dir).player_id == a.player_id);
  CHECK(LoadOrCreateIdentity(b_dir).player_id == b.player_id);
  CHECK(a.player_id.size() == 9 && a.player_id[4] == '-');
  CHECK(ReadFile(a_dir / kIdentityFileName).find(b.token) == std::string::npos);
  a_lock.reset();
  b_lock.reset();
  fs::remove_all(a_dir);
  fs::remove_all(b_dir);
}

void TestOnlineNameCleaning() {
  CHECK(CleanOnlineName("Saulo") == "Saulo");
  CHECK(CleanOnlineName("Sa ulo!") == "Saulo");
  CHECK(CleanOnlineName("A_b-9") == "A_b-9");
  CHECK(CleanOnlineName("").empty() == false);
  CHECK(CleanOnlineName("   ") == "Player");
  CHECK(CleanOnlineName("0123456789ABCDEFGHIJ") == "0123456789ABCDEF");
}

}  // namespace

int main() {
  TestIdentityIsCreatedOnceAndKept();
  TestExistingIdentityIsReadWithoutCreatingOne();
  TestDamagedIdentityIsReportedNotReplaced();
  TestEphemeralIdentityIsNeverWritten();
  TestSecondInstanceCannotTakeTheLock();
  TestTwoInstanceFoldersHaveTheirOwnIdentityAndFriendCode();
  TestOnlineNameCleaning();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
