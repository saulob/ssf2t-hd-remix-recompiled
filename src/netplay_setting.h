#pragma once

namespace ssf2t {

inline constexpr char kNetplayDeterminismSetting[] = "ssf2t_netplay_determinism_test";
inline constexpr bool kDefaultNetplayDeterminism = false;
inline constexpr char kNetplayDeterminismLabel[] = "Netplay Determinism Test";
inline constexpr char kNetplayDeterminismHelp[] =
    "Runs rollback save/load/resimulation checks during Local Versus matches.";

}  // namespace ssf2t
