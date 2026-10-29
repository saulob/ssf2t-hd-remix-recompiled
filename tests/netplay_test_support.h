#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <vector>

#include "netplay/rollback_core.h"

namespace netplay_test {

inline int g_failures = 0;
inline int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++netplay_test::g_checks;                                       \
    if (!(cond)) {                                                  \
      ++netplay_test::g_failures;                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

class Lcg {
 public:
  explicit Lcg(uint64_t seed) : state_(seed) {}
  uint32_t Next() {
    state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
    return uint32_t(state_ >> 33);
  }
  bool Chance(uint32_t percent) { return Next() % 100 < percent; }
  uint32_t Below(uint32_t n) { return n == 0 ? 0 : Next() % n; }

 private:
  uint64_t state_;
};

inline uint64_t Mix(uint64_t state, ssf2t::netplay::InputWord a, ssf2t::netplay::InputWord b) {
  state ^= (uint64_t(a) << 17) ^ (uint64_t(b) << 3) ^ 0x9E3779B97F4A7C15ull;
  state *= 0xFF51AFD7ED558CCDull;
  state ^= state >> 29;
  return state;
}

// A deterministic stand-in for the fight: state depends on every input, effects fire only on frames not replayed.
class ToySim final : public ssf2t::netplay::Simulation {
 public:
  bool Save(std::vector<uint8_t>& out) override {
    if (fail_save) return false;
    out.resize(sizeof(state) + sizeof(counter));
    std::memcpy(out.data(), &state, sizeof(state));
    std::memcpy(out.data() + sizeof(state), &counter, sizeof(counter));
    ++saves;
    return true;
  }
  bool Load(const std::vector<uint8_t>& in) override {
    if (fail_load || in.size() != sizeof(state) + sizeof(counter)) return false;
    std::memcpy(&state, in.data(), sizeof(state));
    std::memcpy(&counter, in.data() + sizeof(state), sizeof(counter));
    ++loads;
    return true;
  }
  bool Advance(const ssf2t::netplay::InputWord (&inputs)[ssf2t::netplay::kPlayers], bool replay) override {
    state = Mix(state, inputs[0], inputs[1]);
    if (corrupt_at != 0 && counter == corrupt_at) state ^= 1;
    ++counter;
    if (replay) {
      ++replayed;
    } else {
      ++effects;
    }
    after[counter] = state;
    return true;
  }
  uint32_t Checksum() override { return uint32_t(state ^ (state >> 32)); }

  uint64_t state = 1;
  uint32_t counter = 0;
  uint32_t effects = 0;
  uint32_t replayed = 0;
  uint32_t saves = 0;
  uint32_t loads = 0;
  uint32_t corrupt_at = 0;
  bool fail_save = false;
  bool fail_load = false;
  std::map<uint32_t, uint64_t> after;
};

// The sample the toy "pad" produces for tick k of player p.
inline ssf2t::netplay::InputWord Sample(int player, uint32_t k) {
  const uint32_t phase = k / 7;
  return ssf2t::netplay::InputWord(((phase * 2654435761u) >> (player ? 5 : 9)) & 0x7FF);
}

// The state after `frames` frames with every input real.
inline uint64_t Reference(uint32_t frames, uint32_t delay) {
  uint64_t state = 1;
  for (uint32_t g = 0; g < frames; ++g) {
    const auto a = g < delay ? ssf2t::netplay::InputWord(0) : Sample(0, g - delay);
    const auto b = g < delay ? ssf2t::netplay::InputWord(0) : Sample(1, g - delay);
    state = Mix(state, a, b);
  }
  return state;
}


using ssf2t::netplay::ChecksumPoint;
using ssf2t::netplay::Frame;
using ssf2t::netplay::InputWord;
using ssf2t::netplay::kWindow;
using ssf2t::netplay::RollbackConfig;
using ssf2t::netplay::RollbackCore;
using ssf2t::netplay::TickStatus;

struct Message {
  uint32_t due = 0;
  Frame next_wanted = 0;
  Frame first = 0;
  std::vector<InputWord> words;
  bool is_check = false;
  ChecksumPoint check;
};

struct Network {
  uint32_t latency = 0;
  uint32_t jitter = 0;
  uint32_t loss_percent = 0;
  uint32_t duplicate_percent = 0;
  bool cut = false;
};

template <class SimT>
struct PeerT {
  SimT sim;
  RollbackCore core;
  uint32_t accepted = 0;
  PeerT(int player, uint32_t delay, uint32_t max_rollback, Frame start, uint32_t interval = 30)
      : core(sim, RollbackConfig{player, delay, max_rollback, interval, start}) {}
};

template <class P>
inline void Send(P& from, std::deque<Message>& wire, const Network& net, Lcg& rng, uint32_t now) {
  if (net.cut) return;
  Message m;
  InputWord words[kWindow];
  const size_t n = from.core.CollectLocalInputs(m.first, words, kWindow);
  m.words.assign(words, words + n);
  m.next_wanted = from.core.remote_wanted();
  const auto emit = [&](const Message& message) {
    if (rng.Chance(net.loss_percent)) return;
    Message copy = message;
    copy.due = now + net.latency + rng.Below(net.jitter + 1);
    wire.push_back(copy);
  };
  emit(m);
  if (rng.Chance(net.duplicate_percent)) emit(m);
  for (const ChecksumPoint& p : from.core.TakeLocalChecksums()) {
    Message c;
    c.is_check = true;
    c.check = p;
    for (int i = 0; i < 3; ++i) emit(c);
  }
}

template <class P>
inline void Deliver(P& to, std::deque<Message>& wire, uint32_t now) {
  // Due messages are taken in queue order, so jitter reorders them.
  for (auto it = wire.begin(); it != wire.end();) {
    if (it->due > now) {
      ++it;
      continue;
    }
    if (it->is_check) {
      to.core.OnRemoteChecksum(it->check.frame, it->check.value);
    } else {
      to.core.OnPeerWanted(it->next_wanted);
      to.core.OnRemoteInputs(it->first, it->words.data(), it->words.size());
    }
    it = wire.erase(it);
  }
}

template <class P>
inline void RunPair(P& a, P& b, uint32_t ticks, const Network& net, uint64_t seed) {
  Lcg rng(seed);
  std::deque<Message> a_to_b, b_to_a;
  for (uint32_t now = 0; now < ticks; ++now) {
    Deliver(a, b_to_a, now);
    Deliver(b, a_to_b, now);
    if (a.core.Tick(Sample(0, a.accepted)) == TickStatus::kAdvanced) ++a.accepted;
    if (b.core.Tick(Sample(1, b.accepted)) == TickStatus::kAdvanced) ++b.accepted;
    Send(a, a_to_b, net, rng, now);
    Send(b, b_to_a, net, rng, now);
  }
}

}  // namespace netplay_test
