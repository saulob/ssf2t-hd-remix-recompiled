#pragma once

#include <string>
#include <vector>

namespace ssf2t::online {

// 127.0.0.1, plus this PC's private LAN address when it has one. These are offered to a peer behind the same router.
std::vector<std::string> LocalCandidates();

bool IsPrivateIPv4(const std::string& ip);

}  // namespace ssf2t::online
