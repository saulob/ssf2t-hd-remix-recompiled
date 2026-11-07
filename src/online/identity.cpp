#include "online/identity.h"

#include <fstream>
#include <system_error>

#include "online/random.h"
#include "updater/sha256.h"

namespace ssf2t::online {
namespace fs = std::filesystem;
namespace {

constexpr char kPlayerAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

bool IsLowerHex(const std::string& s) {
  for (const char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

Identity FromToken(const std::string& token) {
  Identity id;
  id.token = token;
  id.player_id = DerivePlayerId(token);
  return id;
}

Identity Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    Identity id;
    id.error = "cannot read " + file.string();
    return id;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos || line.substr(0, eq) != "token") continue;
    const std::string token = line.substr(eq + 1);
    if (token.size() == 64 && IsLowerHex(token)) return FromToken(token);
  }
  Identity id;
  id.error = file.string() + " is damaged; delete it to make a new online identity";
  return id;
}

}  // namespace

std::string DerivePlayerId(const std::string& token) {
  const std::string digest = trg::update::Sha256Hex("ssf2t-player-id:" + token);
  auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  std::string id;
  for (int i = 0; i < 8; ++i) {
    const int byte = nibble(digest[size_t(i) * 2]) * 16 + nibble(digest[size_t(i) * 2 + 1]);
    id += kPlayerAlphabet[byte % 32];
    if (i == 3) id += '-';
  }
  return id;
}

Identity LoadOrCreateIdentity(const fs::path& user_dir) {
  const fs::path file = user_dir / kIdentityFileName;
  std::error_code ec;
  if (fs::exists(file, ec)) return Read(file);
  const std::string token = RandomHex(32);
  if (token.empty()) {
    Identity id;
    id.error = "the system random generator failed";
    return id;
  }
  fs::create_directories(user_dir, ec);
  const fs::path temp = user_dir / (std::string(kIdentityFileName) + ".new" + RandomHex(3));
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << "; Online identity of this installation. Keep it private: anyone with this file can play as you.\n"
        << "token=" << token << "\n";
    if (!out) {
      Identity id;
      id.error = "cannot write " + temp.string();
      return id;
    }
  }
  // The link fails when another process created the file first; then its identity is the one kept.
  fs::create_hard_link(temp, file, ec);
  if (ec && !fs::exists(file)) {
    ec.clear();
    fs::rename(temp, file, ec);
  }
  fs::remove(temp, ec);
  return Read(file);
}

Identity LoadExistingIdentity(const fs::path& user_dir) {
  const fs::path file = user_dir / kIdentityFileName;
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    Identity id;
    id.error = "no online identity yet";
    return id;
  }
  return Read(file);
}

Identity MakeEphemeralIdentity() {
  const std::string token = RandomHex(32);
  if (token.empty()) {
    Identity id;
    id.error = "the system random generator failed";
    return id;
  }
  Identity id = FromToken(token);
  id.ephemeral = true;
  return id;
}

std::string CleanOnlineName(const std::string& wanted) {
  std::string out;
  for (const char c : wanted) {
    if (out.size() == kMaxOnlineNameChars) break;
    if (OnlineNameChar(c)) out += c;
  }
  return out.empty() ? kDefaultOnlineName : out;
}

}  // namespace ssf2t::online
