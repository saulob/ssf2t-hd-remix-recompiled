// Online fight telemetry: what the client reports about an online fight, and only that. The reporter (one start and one
// completion per fight, never for offline play), the reads of the running fight on both game images, the roster, the
// bounded queue and the service that sends it, and the wire message. Headless: no sockets, no threads, no window.
// Run: ssf2t_fight_telemetry_tests

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "netplay/fight_snapshot.h"
#include "netplay/guest_netplay.h"
#include "online/fight_telemetry.h"
#include "online/online_service.h"
#include "online/protocol.h"

using namespace ssf2t::online;
namespace np = ssf2t::netplay;
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

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

FightSnapshot Fight(int p1, int p2, int wins1 = 0, int wins2 = 0, int to_beat = 1) {
  FightSnapshot s;
  s.valid = true;
  s.character[0] = p1;
  s.character[1] = p2;
  s.wins[0] = wins1;
  s.wins[1] = wins2;
  s.wins_to_beat = to_beat;
  return s;
}

struct Collected {
  std::vector<FightReport> reports;
  bool accept = true;
  FightReporter::Sink Sink() {
    return [this](const FightReport& r) {
      if (!accept) return false;
      reports.push_back(r);
      return true;
    };
  }
  int Count(FightEventKind kind) const {
    int n = 0;
    for (const FightReport& r : reports) n += r.kind == kind ? 1 : 0;
    return n;
  }
};

// Plays one fight through the reporter the way the fight runtime does: armed at the logical start, the fighters still
// at their default values through the setup, then the real characters from the presentation on, then the end.
void PlayFight(FightReporter& reporter, uint32_t generation, uint32_t fight, int p1, int p2, int wins1, int wins2,
               int frames = 60) {
  reporter.Begin(generation, fight);
  for (uint32_t i = 0; i < 10; ++i) reporter.Observe(Fight(0, 0), false, i, 0);
  for (int i = 0; i < frames; ++i) {
    const uint32_t frame = 10 + uint32_t(i);
    reporter.Observe(Fight(p1, p2, wins1, wins2), true, frame, frame);
  }
  reporter.End(Fight(p1, p2, wins1, wins2), true);
}

// ---- the result -------------------------------------------------------------------------------------------------

void TestResultOfTheFight() {
  CHECK(ResultOf(Fight(0, 4, 0, 0)) == FightResult::kNone);
  CHECK(ResultOf(Fight(0, 4, 1, 0)) == FightResult::kNone);  // one round won of three is not a result
  CHECK(ResultOf(Fight(0, 4, 2, 0)) == FightResult::kPlayer1);
  CHECK(ResultOf(Fight(0, 4, 2, 1)) == FightResult::kPlayer1);
  CHECK(ResultOf(Fight(0, 4, 0, 2)) == FightResult::kPlayer2);
  CHECK(ResultOf(Fight(0, 4, 1, 2)) == FightResult::kPlayer2);
  CHECK(ResultOf(Fight(0, 4, 2, 2)) == FightResult::kDraw);
  FightSnapshot invalid = Fight(0, 4, 2, 0);
  invalid.valid = false;
  CHECK(ResultOf(invalid) == FightResult::kNone);
  CHECK(std::strcmp(FightResultName(FightResult::kPlayer1), "p1") == 0);
  CHECK(std::strcmp(FightResultName(FightResult::kPlayer2), "p2") == 0);
  CHECK(std::strcmp(FightResultName(FightResult::kDraw), "draw") == 0);
  CHECK(std::strcmp(FightResultName(FightResult::kNone), "") == 0);
}

// ---- the reporter -----------------------------------------------------------------------------------------------

void TestAnOnlineFightStartsAndCompletesOnce() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 3, 0, 0, 4, 2, 1);
  CHECK(out.reports.size() == 2);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.Count(FightEventKind::kComplete) == 1);
  CHECK(out.reports[0].kind == FightEventKind::kStart && out.reports[0].p1 == 0 && out.reports[0].p2 == 4);
  CHECK(out.reports[0].generation == 3 && out.reports[0].fight == 0);
  CHECK(out.reports[1].kind == FightEventKind::kComplete && out.reports[1].result == FightResult::kPlayer1);
  CHECK(out.reports[1].generation == 3 && out.reports[1].fight == 0 && out.reports[1].p1 == 0 && out.reports[1].p2 == 4);
  reporter.End(Fight(0, 4, 2, 1), true);
  reporter.End(Fight(0, 4, 2, 1), true);
  CHECK(out.reports.size() == 2);  // ending again reports nothing
}

void TestCharactersAreTheOnesHeldWhenTheyBecomeAuthoritative() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  for (uint32_t i = 0; i < 50; ++i) reporter.Observe(Fight(0, 0), false, i, i);  // default values during the setup
  CHECK(out.reports.empty() && reporter.active() && !reporter.started());
  reporter.Observe(Fight(15, 6), true, 50, 50);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.reports[0].p1 == 15 && out.reports[0].p2 == 6);
  for (uint32_t i = 51; i < 120; ++i) reporter.Observe(Fight(15, 6), true, i, i);
  CHECK(out.Count(FightEventKind::kStart) == 1);  // sent once
}

void TestRyuIsAValidCharacterAndNotAMissingOne() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 0, 4, 2, 0);  // Ryu vs Ken
  PlayFight(reporter, 1, 1, 4, 0, 0, 2);  // Ken vs Ryu
  PlayFight(reporter, 1, 2, 0, 0, 2, 0);  // a real mirror
  CHECK(out.reports[0].p1 == 0 && out.reports[0].p2 == 4);
  CHECK(out.reports[2].p1 == 4 && out.reports[2].p2 == 0);
  CHECK(out.reports[4].p1 == 0 && out.reports[4].p2 == 0);
  CHECK(out.Count(FightEventKind::kStart) == 3);
}

void TestDefaultZerosBeforeThePresentationAreNeverReported() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  for (uint32_t i = 0; i < 300; ++i) reporter.Observe(Fight(0, 0, 0, 0), false, i, i);
  CHECK(out.reports.empty());
  reporter.Observe(Fight(0, 4), true, 300, 300);  // Ryu vs Ken, not Ryu vs Ryu
  CHECK(out.reports.size() == 1 && out.reports[0].p1 == 0 && out.reports[0].p2 == 4);
}

void TestAFightEndedBeforeTheCharactersWereReadyReportsNothing() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  for (uint32_t i = 0; i < 20; ++i) reporter.Observe(Fight(0, 0), false, i, i);
  reporter.End(Fight(0, 0), false);
  CHECK(out.reports.empty() && reporter.skipped() == 1 && !reporter.active());
}

void TestTheEndGivesAWaitingStartOneLastAttemptWithoutDuplicating() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 0), false, 1, 1);
  reporter.End(Fight(11, 2, 0, 0), true);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.reports[0].p1 == 11 && out.reports[0].p2 == 2);
  CHECK(out.Count(FightEventKind::kComplete) == 0);
  Collected again;
  FightReporter normal(again.Sink());
  PlayFight(normal, 1, 0, 11, 2, 0, 0);
  CHECK(again.Count(FightEventKind::kStart) == 1);
}

void TestWinnerAndLoserAreTheSidesTheyAre() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 12, 9, 2, 0);
  PlayFight(reporter, 1, 1, 12, 9, 0, 2);
  CHECK(out.reports.size() == 4);
  CHECK(out.reports[1].result == FightResult::kPlayer1 && out.reports[1].p1 == 12 && out.reports[1].p2 == 9);
  CHECK(out.reports[3].result == FightResult::kPlayer2 && out.reports[3].p1 == 12 && out.reports[3].p2 == 9);
}

void TestEveryFinishedScoreIsACompletedFight() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 15, 0, 2, 0);  // observed in the real test: 2:0 with 1 to beat
  PlayFight(reporter, 1, 1, 1, 0, 2, 1);   // observed in the real test: 2:1 with 1 to beat
  PlayFight(reporter, 1, 2, 1, 0, 1, 2);
  CHECK(out.Count(FightEventKind::kComplete) == 3);
  CHECK(out.reports[1].result == FightResult::kPlayer1 && out.reports[3].result == FightResult::kPlayer1);
  CHECK(out.reports[5].result == FightResult::kPlayer2);
}

void TestADrawIsReportedAsADraw() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 2, 3, 2, 2);
  CHECK(out.reports.size() == 2 && out.reports[1].result == FightResult::kDraw);
}

void TestAnAbandonedFightIsStartedButNotCompleted() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 6, 10, 0, 0);  // observed in the real test: left at 0:0
  PlayFight(reporter, 1, 1, 6, 10, 1, 0);
  CHECK(out.Count(FightEventKind::kStart) == 2 && out.Count(FightEventKind::kComplete) == 0);
  CHECK(!reporter.active());
}

void TestAResultIsConfirmedOnlyOnceBothPeersAgreeOnItsFrame() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 4), true, 100, 100);
  reporter.Observe(Fight(0, 4, 2, 0), true, 200, 190);
  CHECK(!reporter.confirmed_result());
  reporter.Observe(Fight(0, 4, 2, 0), true, 210, 199);
  CHECK(!reporter.confirmed_result());
  reporter.Observe(Fight(0, 4, 2, 0), true, 215, 200);
  CHECK(reporter.confirmed_result());
  reporter.End(Fight(0, 4, 2, 0), true);
  CHECK(out.Count(FightEventKind::kComplete) == 1);
}

void TestARollbackThatRewritesTheResultDoesNotDuplicateOrMisreport() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 4), true, 100, 100);
  uint32_t frame = 200;
  for (int round = 0; round < 6; ++round) {  // a speculative win that rollbacks keep taking back
    reporter.Observe(Fight(0, 4, 2, 0), true, frame, frame - 20);
    reporter.Observe(Fight(0, 4, 1, 0), true, frame + 1, frame - 15);
    frame += 5;
  }
  CHECK(!reporter.confirmed_result());
  reporter.Observe(Fight(0, 4, 0, 2), true, frame, frame - 10);  // corrected to the other side
  reporter.Observe(Fight(0, 4, 0, 2), true, frame + 12, frame + 2);
  reporter.End(Fight(0, 4, 0, 2), true);
  CHECK(out.reports.size() == 2);
  CHECK(out.reports[1].result == FightResult::kPlayer2);
}

void TestATransientResultTakenBackBeforeTheEndIsNotCounted() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 4), true, 100, 100);
  reporter.Observe(Fight(0, 4, 2, 0), true, 200, 150);
  reporter.Observe(Fight(0, 4, 1, 0), true, 201, 150);
  reporter.End(Fight(0, 4, 1, 0), true);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.Count(FightEventKind::kComplete) == 0);
}

void TestAFightThatLeavesRightAfterItsResultStillCounts() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(1, 0), true, 100, 100);
  reporter.Observe(Fight(1, 0, 2, 1), true, 400, 380);
  reporter.End(Fight(1, 0, 2, 1), true);
  CHECK(out.Count(FightEventKind::kComplete) == 1 && out.reports[1].result == FightResult::kPlayer1);
  CHECK(!reporter.confirmed_result());
}

void TestARollbackThatReentersTheStartDoesNotDuplicateIt() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 4), true, 100, 100);
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 4), true, 101, 101);
  CHECK(out.Count(FightEventKind::kStart) == 1);
  for (uint32_t i = 0; i < 40; ++i) reporter.Observe(Fight(0, 4, 2, 0), true, 200 + i, 200 + i);
  reporter.Begin(1, 0);  // a Frame Zero retry after the fight is running
  reporter.End(Fight(0, 4, 2, 0), true);
  reporter.Begin(1, 0);  // and after it ended
  reporter.End(Fight(0, 4, 2, 0), true);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.Count(FightEventKind::kComplete) == 1);
}

void TestManyFightsInOneMatchAreManyStarts() {
  Collected out;
  FightReporter reporter(out.Sink());
  for (uint32_t fight = 0; fight < 10; ++fight) PlayFight(reporter, 5, fight, 0, 4, fight % 2 ? 0 : 2, fight % 2 ? 2 : 0);
  CHECK(out.Count(FightEventKind::kStart) == 10 && out.Count(FightEventKind::kComplete) == 10);
  CHECK(reporter.starts() == 10 && reporter.completions() == 10);
  int picks_ryu = 0;
  int picks_ken = 0;
  for (const FightReport& r : out.reports) {
    if (r.kind != FightEventKind::kStart) continue;
    picks_ryu += r.p1 == 0 || r.p2 == 0;
    picks_ken += r.p1 == 4 || r.p2 == 4;
  }
  CHECK(picks_ryu == 10 && picks_ken == 10);
  for (uint32_t fight = 0; fight < 10; ++fight) {
    int starts = 0;
    for (const FightReport& r : out.reports) starts += r.kind == FightEventKind::kStart && r.fight == fight;
    CHECK(starts == 1);
  }
}

void TestAnotherMatchStartsFightZeroAgain() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 0, 4, 2, 0);
  PlayFight(reporter, 2, 0, 0, 4, 2, 0);
  CHECK(out.Count(FightEventKind::kStart) == 2 && out.reports[2].generation == 2);
}

void TestABeginWhileAnotherFightRunsEndsThatFightFirst() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 4), true, 100, 100);
  for (uint32_t i = 0; i < 30; ++i) reporter.Observe(Fight(0, 4, 2, 0), true, 200 + i, 200 + i);
  reporter.Begin(1, 1);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.Count(FightEventKind::kComplete) == 1);
  CHECK(out.reports[1].kind == FightEventKind::kComplete && out.reports[1].fight == 0);
}

void TestAnInvalidCharacterPairCreatesNoStart() {
  Collected out;
  FightReporter reporter(out.Sink());
  uint32_t fight = 0;
  for (const int bad : {-1, 19, 255, 1000}) {
    reporter.Begin(1, fight++);
    reporter.Observe(Fight(bad, 4), true, 10, 10);
    reporter.End(Fight(bad, 4), true);
    reporter.Begin(1, fight++);
    reporter.Observe(Fight(0, bad), true, 10, 10);
    reporter.End(Fight(0, bad), true);
  }
  FightSnapshot unread;
  reporter.Begin(1, fight);
  reporter.Observe(unread, true, 10, 10);
  reporter.End(unread, true);
  CHECK(out.reports.empty() && !reporter.active() && reporter.skipped() == 9);
}

void TestTheStartCharactersStayAuthoritativeWhenTheFightersAreReset() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(0, 1), true, 100, 100);
  for (uint32_t i = 0; i < 30; ++i) reporter.Observe(Fight(0, 1, 2, 1), true, 200 + i, 200 + i);
  reporter.Observe(Fight(0, 0, 2, 1), true, 240, 240);  // the transition back to the menus has reset the fighters
  reporter.End(Fight(0, 0, 2, 1), true);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.Count(FightEventKind::kComplete) == 1);
  CHECK(out.reports[1].result == FightResult::kPlayer1 && out.reports[1].p1 == 0 && out.reports[1].p2 == 1);
  CHECK(reporter.character(0) == 0 && reporter.character(1) == 1 && reporter.result() == FightResult::kPlayer1);
}

void TestAFightLeavingWithResetFightersAndNoCleanObservationStillCounts() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(13, 0), true, 100, 100);
  reporter.Observe(Fight(0, 0, 2, 0), true, 300, 280);
  reporter.End(Fight(0, 0, 2, 0), true);
  CHECK(out.Count(FightEventKind::kComplete) == 1 && out.reports[1].p1 == 13 && out.reports[1].p2 == 0);
}

void TestAConfirmedResultSurvivesTheScoresBeingReset() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(6, 10), true, 100, 100);
  reporter.Observe(Fight(6, 10, 2, 1), true, 200, 200);
  reporter.Observe(Fight(6, 10, 0, 0), true, 210, 210);
  reporter.End(Fight(6, 10, 0, 0), true);
  CHECK(out.Count(FightEventKind::kComplete) == 1 && out.reports[1].result == FightResult::kPlayer1);
}

void TestAFightLeftBeforeAnyWinnerIsStartedOnly() {
  Collected out;
  FightReporter reporter(out.Sink());
  reporter.Begin(1, 0);
  reporter.Observe(Fight(6, 10), true, 100, 100);
  reporter.Observe(Fight(6, 10, 1, 0), true, 300, 300);
  reporter.End(Fight(6, 10, 1, 0), true);
  CHECK(out.Count(FightEventKind::kStart) == 1 && out.Count(FightEventKind::kComplete) == 0);
}

void TestAFailingSinkNeverDisturbsTheFight() {
  Collected out;
  out.accept = false;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, 0, 4, 2, 0);
  PlayFight(reporter, 1, 1, 0, 4, 2, 0);
  CHECK(out.reports.empty() && reporter.dropped() == 4 && reporter.starts() == 2 && reporter.completions() == 2);
  FightReporter none{FightReporter::Sink()};
  PlayFight(none, 1, 0, 0, 4, 2, 0);
  CHECK(none.dropped() == 2);
}

void TestOnlyAnOnlineFightIsReportable() {
  constexpr uint8_t kOnline = 3;
  CHECK(FightReportable(true, kOnline, kOnline));
  CHECK(!FightReportable(false, kOnline, kOnline));  // no online match: offline play
  for (const uint8_t mode : {0, 1, 2, 4, 5, 255}) CHECK(!FightReportable(true, mode, kOnline));  // Local Match, Arcade, Training...
  CHECK(!FightReportable(false, 0, kOnline));
}

void TestTheQueueIsBoundedAndDropsTheNewest() {
  FightReportQueue queue;
  FightReport report;
  for (size_t i = 0; i < FightReportQueue::kCapacity; ++i) {
    report.fight = uint32_t(i);
    CHECK(queue.Push(report));
  }
  report.fight = 999;
  CHECK(!queue.Push(report) && !queue.Push(report));
  CHECK(queue.size() == FightReportQueue::kCapacity && queue.dropped() == 2);
  for (size_t i = 0; i < FightReportQueue::kCapacity; ++i) {
    const auto popped = queue.Pop();
    CHECK(popped && popped->fight == i);
  }
  CHECK(!queue.Pop() && queue.size() == 0);
  CHECK(queue.Push(report));  // and it works again once drained
}

// ---- the roster -------------------------------------------------------------------------------------------------

std::string Letters(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

constexpr int kGuestRoster = 17;

void TestTheRosterIsTheSeventeenFightersAndEvilRyu() {
  CHECK(kCharacterCount == kGuestRoster + 2 && kAkumaCharacter == 16 && kEvilRyuCharacter == 17 &&
        kViolentKenCharacter == 18);
  const char* expected[] = {"Ryu",     "E. Honda", "Blanka", "Guile",   "Ken",     "Chun-Li", "Zangief", "Dhalsim", "M. Bison",
                            "Sagat",   "Balrog",   "Vega",   "Cammy",   "T. Hawk", "Fei Long", "Dee Jay", "Akuma", "Evil Ryu", "Violent Ken"};
  for (int id = 0; id < kCharacterCount; ++id) CHECK(std::strcmp(CharacterName(id), expected[id]) == 0);
  CHECK(std::strcmp(CharacterName(-1), "") == 0 && std::strcmp(CharacterName(kCharacterCount), "") == 0);
  CHECK(IsCharacterId(0) && IsCharacterId(16) && IsCharacterId(18) && !IsCharacterId(19) && !IsCharacterId(-1));
}

void TestTheRosterMatchesTheWorker() {
  std::ifstream in(fs::path(SSF2T_SOURCE_DIR) / "server" / "ssf2t-lobby" / "src" / "characters.js");
  if (!in) {
    std::printf("  SKIP Worker roster: characters.js not found\n");
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const size_t begin = text.find('[');
  const size_t end = text.find(']', begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  std::vector<std::string> names;
  for (size_t at = begin; at < end;) {
    const size_t open = text.find('\'', at);
    if (open == std::string::npos || open > end) break;
    const size_t close = text.find('\'', open + 1);
    names.push_back(text.substr(open + 1, close - open - 1));
    at = close + 1;
  }
  CHECK(int(names.size()) >= kCharacterCount);
  for (int id = 0; id < kCharacterCount && id < int(names.size()); ++id) CHECK(names[id] == CharacterName(id));
}

// The game's own name table (id to name) in an image: pointers at 0x83A21280, the 18th names the count.
struct Image {
  std::vector<uint8_t> bytes;
  explicit Image(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  bool loaded() const { return !bytes.empty(); }
  uint8_t U8(uint32_t address) const {
    const size_t at = address - 0x82000000u;
    return at < bytes.size() ? bytes[at] : 0;
  }
  uint16_t U16(uint32_t address) const { return uint16_t(U8(address) << 8 | U8(address + 1)); }
  uint32_t U32(uint32_t address) const { return uint32_t(U16(address)) << 16 | U16(address + 2); }
  std::string Text(uint32_t address) const {
    std::string out;
    for (uint32_t i = 0; i < 40 && U8(address + i); ++i) out += char(U8(address + i));
    return out;
  }
};

constexpr uint32_t kGameNameTable = 0x83A21280;

void CheckNameTable(const Image& image, const char* label) {
  for (int id = 0; id < kGuestRoster; ++id) {
    const std::string game = Letters(image.Text(image.U32(kGameNameTable + 4 * id)));
    const std::string ours = Letters(CharacterName(id));
    CHECK(!game.empty() && ours.size() >= game.size() && ours.compare(ours.size() - game.size(), game.size(), game) == 0);
    if (ours.size() < game.size() || ours.compare(ours.size() - game.size(), game.size(), game) != 0) {
      std::printf("    %s id %d: game \"%s\" roster \"%s\"\n", label, id, game.c_str(), ours.c_str());
    }
  }
  CHECK(image.Text(image.U32(kGameNameTable + 4 * kGuestRoster)) == "NUM_CHARACTERS");
}

void TestTheRosterIsTheGamesOwnIdTableInBothImages() {
  const fs::path root(SSF2T_SOURCE_DIR);
  const Image base(root / "game" / "analysis" / "default-0.0.1.3.image.bin");
  const Image tu(root / "game" / "analysis" / "default.image.bin");
  if (!base.loaded() || !tu.loaded()) {
    std::printf("  SKIP game name tables: analysis images not found\n");
    return;
  }
  CheckNameTable(base, "base013");
  CheckNameTable(tu, "tu023");
}

// ---- the reads of the running fight, per game image ---------------------------------------------------------------

struct MapMemory {
  std::map<uint32_t, uint8_t> bytes;
  void Set8(uint32_t a, uint8_t v) { bytes[a] = v; }
  void Set16(uint32_t a, uint16_t v) { Set8(a, uint8_t(v >> 8)); Set8(a + 1, uint8_t(v)); }
  void Set32(uint32_t a, uint32_t v) { Set16(a, uint16_t(v >> 16)); Set16(a + 2, uint16_t(v)); }
  uint8_t U8(uint32_t a) const { const auto it = bytes.find(a); return it == bytes.end() ? 0 : it->second; }
  uint16_t U16(uint32_t a) const { return uint16_t(U8(a) << 8 | U8(a + 1)); }
  uint32_t U32(uint32_t a) const { return uint32_t(U16(a)) << 16 | U16(a + 2); }
};

constexpr uint32_t kBlock = 0xFC2A0000;
constexpr uint32_t kWork = 0x40100000;

template <typename G>
void FillFight(MapMemory& m, int c1, int c2, int w1, int w2, int to_beat, bool hidden1 = false) {
  m.Set32(G::kFighterBlockPointer, kBlock);
  m.Set32(G::kFightWork, kWork);
  m.Set8(kBlock + 0x700 + 0x39D, uint8_t(c1));
  m.Set8(kBlock + 0xB00 + 0x39D, uint8_t(c2));
  m.Set8(kBlock + 0x700 + 0x3C1, hidden1 ? 1 : 0);
  m.Set8(kBlock + 0x700 + 0x39C, uint8_t(w1));
  m.Set8(kBlock + 0xB00 + 0x39C, uint8_t(w2));
  m.Set16(kWork + 0x6DA, uint16_t(to_beat));
}

template <typename G>
void CheckGuestReads() {
  for (int id = 0; id < kGuestRoster - 1; ++id) {  // every id but Akuma comes from the stored id
    MapMemory m;
    FillFight<G>(m, id, (id + 5) % 16, 0, 0, 1);
    const FightSnapshot s = np::ReadFightSnapshot<G>(m);
    CHECK(s.valid && s.character[0] == id);
  }
  MapMemory m;
  FillFight<G>(m, 6, 11, 2, 1, 1);
  FightSnapshot s = np::ReadFightSnapshot<G>(m);
  CHECK(s.valid && s.character[0] == 6 && s.character[1] == 11 && s.wins[0] == 2 && s.wins[1] == 1 && s.wins_to_beat == 1);
  CHECK(ResultOf(s) == FightResult::kPlayer1);

  MapMemory akuma;
  FillFight<G>(akuma, 3, 4, 0, 0, 1, true);  // the hidden flag makes the first fighter Akuma whatever id it holds
  s = np::ReadFightSnapshot<G>(akuma);
  CHECK(s.valid && s.character[0] == kAkumaCharacter && s.character[1] == 4);

  MapMemory bad;
  FillFight<G>(bad, 17, 4, 0, 0, 1);
  CHECK(!np::ReadFightSnapshot<G>(bad).valid);
  MapMemory negative;
  FillFight<G>(negative, 0xFF, 4, 0, 0, 1);
  CHECK(!np::ReadFightSnapshot<G>(negative).valid);

  MapMemory unready;  // the fight objects are not there yet
  CHECK(!np::ReadFightSnapshot<G>(unready).valid);
  MapMemory no_work;
  no_work.Set32(G::kFighterBlockPointer, kBlock);
  CHECK(!np::ReadFightSnapshot<G>(no_work).valid);

  MapMemory negative_wins;
  FillFight<G>(negative_wins, 0, 4, 0xFF, 0x80, 1);
  s = np::ReadFightSnapshot<G>(negative_wins);
  CHECK(s.valid && s.wins[0] == 0 && s.wins[1] == 0);
}

void TestBothGuestsReadTheirOwnAddresses() {
  CheckGuestReads<np::Base013Netplay>();
  CheckGuestReads<np::TitleUpdate023Netplay>();

  // The addresses are each image's own: a fight laid out for one is not readable through the other.
  MapMemory base;
  FillFight<np::Base013Netplay>(base, 0, 4, 2, 0, 1);
  CHECK(np::ReadFightSnapshot<np::Base013Netplay>(base).valid);
  CHECK(!np::ReadFightSnapshot<np::TitleUpdate023Netplay>(base).valid);
  MapMemory tu;
  FillFight<np::TitleUpdate023Netplay>(tu, 0, 4, 2, 0, 1);
  CHECK(np::ReadFightSnapshot<np::TitleUpdate023Netplay>(tu).valid);
  CHECK(!np::ReadFightSnapshot<np::Base013Netplay>(tu).valid);
  CHECK(np::Base013Netplay::kFighterBlockPointer != np::TitleUpdate023Netplay::kFighterBlockPointer);
  CHECK(np::Base013Netplay::kRoundWinPlayer1 != np::TitleUpdate023Netplay::kRoundWinPlayer1);
  CHECK(np::Base013Netplay::kRoundWinPlayer2 != np::TitleUpdate023Netplay::kRoundWinPlayer2);
  CHECK(np::Base013Netplay::kFightCharacterInit != np::TitleUpdate023Netplay::kFightCharacterInit);
}

template <typename G>
void CheckCodeOnImage(const Image& image, const char* label) {
  CHECK(np::FightTelemetryCodeMatches<G>(image));
  if (!np::FightTelemetryCodeMatches<G>(image)) std::printf("    %s: the analysed fight code is not in the image\n", label);
}

void TestTheAnalysedCodeIsInEachImageIndependently() {
  const fs::path root(SSF2T_SOURCE_DIR);
  const Image base(root / "game" / "analysis" / "default-0.0.1.3.image.bin");
  const Image tu(root / "game" / "analysis" / "default.image.bin");
  if (!base.loaded() || !tu.loaded()) {
    std::printf("  SKIP fight code fingerprints: analysis images not found\n");
    return;
  }
  CheckCodeOnImage<np::Base013Netplay>(base, "base013");
  CheckCodeOnImage<np::TitleUpdate023Netplay>(tu, "tu023");
  // Neither guest's addresses describe the other image.
  CHECK(!np::FightTelemetryCodeMatches<np::Base013Netplay>(tu));
  CHECK(!np::FightTelemetryCodeMatches<np::TitleUpdate023Netplay>(base));
  // A shifted function is not the analysed one.
  struct Shifted {
    const Image& image;
    uint32_t shift;
    uint8_t U8(uint32_t a) const { return image.U8(a + shift); }
    uint16_t U16(uint32_t a) const { return image.U16(a + shift); }
    uint32_t U32(uint32_t a) const { return image.U32(a + shift); }
  };
  CHECK(!np::FightTelemetryCodeMatches<np::Base013Netplay>(Shifted{base, 0x2000}));
  CHECK(!np::FightTelemetryCodeMatches<np::TitleUpdate023Netplay>(Shifted{tu, 0x2000}));
}

// ---- the service ------------------------------------------------------------------------------------------------

constexpr char kToken[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kFriend[] = "ABCD-2345";
constexpr char kSession[] = "00112233445566aa";

struct ChannelState {
  std::deque<ChannelEvent> in;
  std::vector<std::string> sent;
  bool closed = false;
};

class FakeChannel final : public LobbyChannel {
 public:
  explicit FakeChannel(std::shared_ptr<ChannelState> state) : state_(std::move(state)) {}
  bool Poll(ChannelEvent& out) override {
    if (state_->in.empty()) return false;
    out = std::move(state_->in.front());
    state_->in.pop_front();
    return true;
  }
  void Send(const std::string& text) override { state_->sent.push_back(text); }
  bool SendBinary(std::vector<uint8_t>&&) override { return !state_->closed; }
  void Close() override { state_->closed = true; }

 private:
  std::shared_ptr<ChannelState> state_;
};

int SeqOf(const std::string& request) {
  const size_t at = request.find("\"seq\":");
  return at == std::string::npos ? 0 : std::atoi(request.c_str() + at + 6);
}

struct Rig {
  std::vector<std::shared_ptr<ChannelState>> channels;
  std::vector<std::string> logs;
  std::unique_ptr<OnlineService> service;
  std::string me;
  std::string guest_name;
  double now = 100;

  explicit Rig(bool telemetry = true, const std::string& guest = kGuestBase013) : guest_name(guest) {
    ServiceConfig config;
    config.url = "wss://example.test/";
    config.guest = guest;
    config.version = "0.3.1";
    config.platform = "Windows";
    config.synchronous_probes = true;
    config.fight_telemetry = telemetry;
    config.identity = [] {
      Identity id;
      id.token = kToken;
      id.player_id = DerivePlayerId(kToken);
      return id;
    };
    config.log = [this](const std::string& line) { logs.push_back(line); };
    config.open_channel = [this](const std::string&) {
      auto state = std::make_shared<ChannelState>();
      channels.push_back(state);
      return std::unique_ptr<LobbyChannel>(new FakeChannel(state));
    };
    service = std::make_unique<OnlineService>(config);
    me = DerivePlayerId(kToken);
  }

  ChannelState& Last() { return *channels.back(); }
  void Tick(double seconds = 0.1) {
    now += seconds;
    service->Update(now);
  }
  void Feed(const std::string& text) {
    Last().in.push_back({ChannelEvent::Kind::kText, text});
    Tick(0.01);
  }
  std::string LastRequest(const std::string& op) {
    for (auto it = Last().sent.rbegin(); it != Last().sent.rend(); ++it) {
      if (Contains(*it, "\"op\":\"" + op + "\"")) return *it;
    }
    return {};
  }
  int Count(const std::string& op) {
    int n = 0;
    for (const std::string& s : Last().sent) n += Contains(s, "\"op\":\"" + op + "\"") ? 1 : 0;
    return n;
  }
  int CountLogs(const std::string& part) const {
    int n = 0;
    for (const std::string& line : logs) n += Contains(line, part) ? 1 : 0;
    return n;
  }
  void Reply(const std::string& op, const std::string& body = {}, const char* code = "ok") {
    const std::string request = LastRequest(op);
    CHECK(!request.empty());
    Feed("{\"re\":" + std::to_string(SeqOf(request)) + ",\"code\":\"" + code + "\"" + (body.empty() ? "" : "," + body) + "}");
  }

  void SignIn(bool announce = true) {
    service->Apply({true, "Saulo"});
    Tick();
    Last().in.push_back({ChannelEvent::Kind::kOpen, {}});
    Tick(0.01);
    std::string caps;
    if (announce) {
      caps = ",\"capabilities\":{\"protocol_version\":3,\"worker_build\":\"test\",\"public_endpoints\":true,"
             "\"relay_supported\":true,\"relay_max_payload\":1472,\"fight_telemetry\":true}";
    }
    Reply("hello", "\"player\":\"" + me + "\",\"name\":\"Saulo\",\"friend_code\":\"" + me + "\"" + caps);
  }

  // A match with the peer already signalled, so the fight session has its assignment.
  void StartMatch(const char* role, bool announce = true) {
    SignIn(announce);
    Feed(std::string("{\"ev\":\"match.created\",\"session\":\"") + kSession + "\",\"match\":\"m1\",\"role\":\"" + role +
         "\",\"guest\":\"" + guest_name + "\",\"build\":1,\"peer\":{\"player\":\"" + kFriend + "\",\"name\":\"SauloB\"}}");
    Feed(std::string("{\"ev\":\"match.peer\",\"match\":\"m1\",\"player\":\"") + kFriend +
         "\",\"name\":\"SauloB\",\"role\":\"" + (std::string(role) == "host" ? "join" : "host") +
         "\",\"ip\":\"203.0.113.9\",\"port\":7002,\"lan\":[\"127.0.0.1\"],\"same_network\":false}");
  }
};

FightReport Start(uint32_t generation = 1, uint32_t fight = 0, int p1 = 0, int p2 = 4) {
  FightReport r;
  r.kind = FightEventKind::kStart;
  r.generation = generation;
  r.fight = fight;
  r.p1 = p1;
  r.p2 = p2;
  return r;
}

FightReport Complete(uint32_t generation = 1, uint32_t fight = 0, FightResult result = FightResult::kPlayer1) {
  FightReport r = Start(generation, fight);
  r.kind = FightEventKind::kComplete;
  r.result = result;
  return r;
}

void TestTheHelloAnnouncesTelemetryOnlyWhenItIsOn() {
  Rig on(true);
  on.SignIn();
  CHECK(Contains(on.LastRequest("hello"), "\"telemetry\":true"));
  Rig off(false);
  off.SignIn();
  CHECK(!Contains(off.LastRequest("hello"), "telemetry"));
  CHECK(on.service->Capabilities().fight_telemetry);
}

void TestTheHostSendsTheStartAndTheCompletionOfADirectMatch() {
  Rig rig;
  rig.StartMatch("host");
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && assignment->host && !assignment->relay);
  CHECK(rig.service->ReportFight(Start(assignment->generation, 0, 0, 4)));
  rig.Tick();
  const std::string start = rig.LastRequest("telemetry.fight");
  CHECK(Contains(start, "\"match\":\"m1\"") && Contains(start, "\"kind\":\"start\"") && Contains(start, "\"fight\":0"));
  CHECK(Contains(start, "\"p1\":0,\"p2\":4") && Contains(start, "\"build\":\"base013\"") &&
        Contains(start, "\"transport\":\"direct\"") && Contains(start, "\"platform\":\"Windows\""));
  CHECK(!Contains(start, "result"));
  CHECK(rig.service->ReportFight(Complete(assignment->generation, 0, FightResult::kPlayer2)));
  rig.Tick();
  const std::string done = rig.LastRequest("telemetry.fight");
  CHECK(Contains(done, "\"kind\":\"complete\"") && Contains(done, "\"result\":\"p2\"") && Contains(done, "\"fight\":0"));
  CHECK(rig.Count("telemetry.fight") == 2);
  rig.Reply("telemetry.fight");
  CHECK(rig.CountLogs("fight telemetry refused") == 0);
}

void TestTheTransportOfAReportIsTheOneTheFightWasHanded() {
  NetplayAssignment direct;
  direct.relay = false;
  NetplayAssignment relay;
  relay.relay = true;
  CHECK(std::strcmp(FightTransportOf(direct), "direct") == 0);
  CHECK(std::strcmp(FightTransportOf(relay), "relay") == 0);
}

void TestTheJoinerNeverReports() {
  Rig rig;
  rig.StartMatch("join");
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && !assignment->host);
  CHECK(rig.service->ReportFight(Start(assignment->generation)));
  rig.Tick();
  CHECK(rig.Count("telemetry.fight") == 0 && rig.CountLogs("fight telemetry dropped") == 1);
}

void TestAnOlderServiceIsNeverSentTelemetry() {
  Rig rig;
  rig.StartMatch("host", false);
  const auto assignment = rig.service->Assignment();
  CHECK(assignment && !rig.service->Capabilities().fight_telemetry);
  rig.service->ReportFight(Start(assignment->generation));
  rig.Tick();
  CHECK(rig.Count("telemetry.fight") == 0);
}

void TestAReportOfAnotherMatchOrNoMatchIsDropped() {
  Rig rig;
  rig.StartMatch("host");
  const auto assignment = rig.service->Assignment();
  rig.service->ReportFight(Start(assignment->generation + 1));
  rig.service->ReportFight(Start(assignment->generation - 1));
  rig.Tick();
  CHECK(rig.Count("telemetry.fight") == 0 && rig.CountLogs("fight telemetry dropped") == 2);

  Rig offline;
  offline.service->Apply({false, "Saulo"});
  CHECK(offline.service->ReportFight(Start()));  // queued, never sent
  offline.Tick();
  CHECK(offline.channels.empty());

  Rig signed_out;
  CHECK(signed_out.service->ReportFight(Start()));
  signed_out.SignIn();
  signed_out.Tick();
  CHECK(signed_out.Count("telemetry.fight") == 0);
}

void TestATelemetryOffClientQueuesAndSendsNothing() {
  Rig rig(false);
  rig.StartMatch("host");
  CHECK(!rig.service->ReportFight(Start(rig.service->Assignment()->generation)));
  rig.Tick();
  CHECK(rig.Count("telemetry.fight") == 0);
}

void TestLeavingTheMatchStillSendsTheLastReport() {
  Rig rig;
  rig.StartMatch("host");
  const uint32_t generation = rig.service->Assignment()->generation;
  rig.service->ReportFight(Complete(generation, 3));
  rig.service->LeaveMatch();
  CHECK(rig.Count("telemetry.fight") == 1);
  const auto& sent = rig.Last().sent;
  size_t report = 0, leave = 0;
  for (size_t i = 0; i < sent.size(); ++i) {
    if (Contains(sent[i], "telemetry.fight")) report = i;
    if (Contains(sent[i], "match.leave")) leave = i;
  }
  CHECK(report > 0 && leave > report);
}

void TestAFullQueueNeverBlocksTheFight() {
  Rig rig;
  rig.StartMatch("host");
  const uint32_t generation = rig.service->Assignment()->generation;
  int accepted = 0;
  for (uint32_t i = 0; i < 200; ++i) accepted += rig.service->ReportFight(Start(generation, i)) ? 1 : 0;
  CHECK(accepted == int(FightReportQueue::kCapacity));
  rig.Tick();
  CHECK(rig.Count("telemetry.fight") == int(FightReportQueue::kCapacity));
  CHECK(rig.service->ReportFight(Start(generation, 500)));  // drained, so accepted again
}

void TestARefusedReportIsLoggedAndIgnored() {
  Rig rig;
  rig.StartMatch("host");
  rig.service->ReportFight(Start(rig.service->Assignment()->generation));
  rig.Tick();
  rig.Reply("telemetry.fight", {}, "bad_request");
  CHECK(rig.CountLogs("fight telemetry refused code=bad_request") == 1);
  CHECK(rig.service->snapshot().connection == Connection::kOnline);
  CHECK(rig.service->snapshot().match.has_value());
}

void TestTheWireMessageIsExactly() {
  FightEventFields e;
  e.kind = "complete";
  e.fight = 9;
  e.p1 = 16;
  e.p2 = 3;
  e.build = "tu023";
  e.transport = "relay";
  e.platform = "Linux";
  e.result = "draw";
  const std::string text = EncodeTelemetryFight(12, "m-1", e);
  CHECK(text == "{\"op\":\"telemetry.fight\",\"seq\":12,\"match\":\"m-1\",\"kind\":\"complete\",\"fight\":9,\"p1\":16,\"p2\":3,"
                "\"build\":\"tu023\",\"transport\":\"relay\",\"platform\":\"Linux\",\"result\":\"draw\"}");
  e.kind = "start";
  e.result.clear();
  CHECK(!Contains(EncodeTelemetryFight(1, "m-1", e), "result"));
  // Nothing in it names a player, an address or a name.
  for (const char* secret : {"token", "player", "name", "ip", "friend", "code"}) CHECK(!Contains(text.substr(text.find("telemetry.fight") + 15), secret));
}

}  // namespace

// Evil Ryu plays on Ryu's guest id but is reported as his own character, so the dashboard never counts him as Ryu.
void TestEvilRyuIsReportedApartFromRyu() {
  Collected out;
  FightReporter reporter(out.Sink());
  PlayFight(reporter, 1, 0, kEvilRyuCharacter, 0, 2, 0);
  PlayFight(reporter, 1, 1, kEvilRyuCharacter, kEvilRyuCharacter, 0, 2);
  PlayFight(reporter, 1, 2, kAkumaCharacter, kEvilRyuCharacter, 2, 1);
  CHECK(out.Count(FightEventKind::kStart) == 3 && out.Count(FightEventKind::kComplete) == 3);
  CHECK(out.reports[0].p1 == 17 && out.reports[0].p2 == 0);
  CHECK(out.reports[2].p1 == 17 && out.reports[2].p2 == 17);
  CHECK(out.reports[4].p1 == 16 && out.reports[4].p2 == 17);
  CHECK(std::strcmp(CharacterName(out.reports[0].p1), "Evil Ryu") == 0 && std::strcmp(CharacterName(out.reports[0].p2), "Ryu") == 0);
  FightEventFields e;
  e.kind = "start";
  e.p1 = 17;
  e.p2 = 16;
  e.build = "base013";
  e.transport = "direct";
  const std::string text = EncodeTelemetryFight(1, "m-1", e);
  CHECK(Contains(text, "\"p1\":17") && Contains(text, "\"p2\":16"));
}

int main() {
  std::printf("fight telemetry tests\n");
  TestEvilRyuIsReportedApartFromRyu();
  TestResultOfTheFight();
  TestAnOnlineFightStartsAndCompletesOnce();
  TestCharactersAreTheOnesHeldWhenTheyBecomeAuthoritative();
  TestRyuIsAValidCharacterAndNotAMissingOne();
  TestDefaultZerosBeforeThePresentationAreNeverReported();
  TestAFightEndedBeforeTheCharactersWereReadyReportsNothing();
  TestTheEndGivesAWaitingStartOneLastAttemptWithoutDuplicating();
  TestWinnerAndLoserAreTheSidesTheyAre();
  TestEveryFinishedScoreIsACompletedFight();
  TestADrawIsReportedAsADraw();
  TestAnAbandonedFightIsStartedButNotCompleted();
  TestAResultIsConfirmedOnlyOnceBothPeersAgreeOnItsFrame();
  TestARollbackThatRewritesTheResultDoesNotDuplicateOrMisreport();
  TestATransientResultTakenBackBeforeTheEndIsNotCounted();
  TestAFightThatLeavesRightAfterItsResultStillCounts();
  TestARollbackThatReentersTheStartDoesNotDuplicateIt();
  TestManyFightsInOneMatchAreManyStarts();
  TestAnotherMatchStartsFightZeroAgain();
  TestABeginWhileAnotherFightRunsEndsThatFightFirst();
  TestAnInvalidCharacterPairCreatesNoStart();
  TestTheStartCharactersStayAuthoritativeWhenTheFightersAreReset();
  TestAFightLeavingWithResetFightersAndNoCleanObservationStillCounts();
  TestAConfirmedResultSurvivesTheScoresBeingReset();
  TestAFightLeftBeforeAnyWinnerIsStartedOnly();
  TestAFailingSinkNeverDisturbsTheFight();
  TestOnlyAnOnlineFightIsReportable();
  TestTheQueueIsBoundedAndDropsTheNewest();
  TestTheRosterIsTheSeventeenFightersAndEvilRyu();
  TestTheRosterMatchesTheWorker();
  TestTheRosterIsTheGamesOwnIdTableInBothImages();
  TestBothGuestsReadTheirOwnAddresses();
  TestTheAnalysedCodeIsInEachImageIndependently();
  TestTheHelloAnnouncesTelemetryOnlyWhenItIsOn();
  TestTheHostSendsTheStartAndTheCompletionOfADirectMatch();
  TestTheTransportOfAReportIsTheOneTheFightWasHanded();
  TestTheJoinerNeverReports();
  TestAnOlderServiceIsNeverSentTelemetry();
  TestAReportOfAnotherMatchOrNoMatchIsDropped();
  TestATelemetryOffClientQueuesAndSendsNothing();
  TestLeavingTheMatchStillSendsTheLastReport();
  TestAFullQueueNeverBlocksTheFight();
  TestARefusedReportIsLoggedAndIgnored();
  TestTheWireMessageIsExactly();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
