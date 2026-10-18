#include "trg/xbox360.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <vector>

#include "trg/platform.h"
#include "utf8_path.h"

namespace trg {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kSector = 2048;
// Where the game partition starts: plain XISO, XGD2, XGD1 and XGD3 images.
constexpr uint64_t kPartitions[] = {0, 0xFD90000, 0x2080000, 0x18300000};
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

using detail::Utf8Path;

struct Entry {
  std::string name;
  uint32_t sector = 0, size = 0;
  bool dir = false;
};

// A name from the disc that is safe to create inside the output folder.
bool SafeName(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (unsigned char c : name)
    if (c < 0x20 || c == '/' || c == '\\' || c == ':') return false;
  return true;
}

class Disc {
 public:
  bool Open(const std::string& path) {
    f_.open(Utf8Path(path), std::ios::binary);
    if (!f_) return false;
    for (uint64_t p : kPartitions) {
      char m[20];
      if (Read(p + 0x10000, m, sizeof(m)) && !std::memcmp(m, kMagic, sizeof(m))) {
        base_ = p;
        uint8_t vd[0x20];
        Read(p + 0x10000, vd, sizeof(vd));
        std::memcpy(&root_sector_, vd + 0x14, 4);
        std::memcpy(&root_size_, vd + 0x18, 4);
        return true;
      }
    }
    return false;
  }

  // A directory is a binary tree of entries; walk it without recursion.
  std::vector<Entry> List(uint32_t sector, uint32_t size) {
    std::vector<Entry> out;
    if (!size || size > (64u << 20)) return out;
    std::vector<uint8_t> t(size);
    if (!Read(base_ + uint64_t(sector) * kSector, t.data(), size)) return out;
    std::vector<size_t> stack = {0};
    std::set<size_t> seen;
    while (!stack.empty()) {
      const size_t o = stack.back();
      stack.pop_back();
      if (o + 14 > t.size() || !seen.insert(o).second) continue;
      uint16_t left, right;
      std::memcpy(&left, &t[o], 2);
      std::memcpy(&right, &t[o + 2], 2);
      if (left == 0xFFFF && right == 0xFFFF) continue;
      Entry e;
      std::memcpy(&e.sector, &t[o + 4], 4);
      std::memcpy(&e.size, &t[o + 8], 4);
      e.dir = (t[o + 12] & 0x10) != 0;
      const size_t len = t[o + 13];
      if (o + 14 + len > t.size()) continue;
      e.name.assign(reinterpret_cast<const char*>(&t[o + 14]), len);
      out.push_back(std::move(e));
      if (left && left != 0xFFFF) stack.push_back(size_t(left) * 4);
      if (right && right != 0xFFFF) stack.push_back(size_t(right) * 4);
    }
    return out;
  }

  bool Read(uint64_t offset, void* dst, size_t n) {
    f_.clear();
    f_.seekg(std::streamoff(offset));
    f_.read(static_cast<char*>(dst), std::streamsize(n));
    return size_t(f_.gcount()) == n;
  }

  uint64_t base() const { return base_; }
  uint32_t root_sector() const { return root_sector_; }
  uint32_t root_size() const { return root_size_; }

 private:
  std::ifstream f_;
  uint64_t base_ = 0;
  uint32_t root_sector_ = 0, root_size_ = 0;
};

uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

}  // namespace

uint32_t ReadXbox360TitleId(const std::string& utf8_image) {
  Disc d;
  if (!d.Open(utf8_image)) return 0;
  for (const Entry& e : d.List(d.root_sector(), d.root_size())) {
    if (e.dir || e.name.size() != 11 ||
        !std::equal(e.name.begin(), e.name.end(), "default.xex",
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
      continue;
    std::vector<uint8_t> h(std::min<uint32_t>(e.size, 0x4000));
    if (h.size() < 0x18 || !d.Read(d.base() + uint64_t(e.sector) * kSector, h.data(), h.size()) ||
        std::memcmp(h.data(), "XEX2", 4))
      return 0;
    // Optional headers: (key, value) pairs; 0x00040006 = execution info, title ID at +12.
    const uint32_t count = BE32(&h[0x14]);
    for (uint32_t i = 0; i < count && 0x18 + size_t(i) * 8 + 8 <= h.size(); ++i) {
      const uint32_t key = BE32(&h[0x18 + i * 8]), value = BE32(&h[0x18 + i * 8 + 4]);
      if (key == 0x00040006 && size_t(value) + 16 <= h.size()) return BE32(&h[value + 12]);
    }
    return 0;
  }
  return 0;
}

std::string ExtractXbox360Disc(Task& task, const std::string& utf8_image, const std::string& utf8_out_dir) {
  Disc d;
  if (!d.Open(utf8_image)) return "That file is not an Xbox 360 disc image.";
  uint64_t total = 0;
  std::function<void(uint32_t, uint32_t, int)> count = [&](uint32_t s, uint32_t n, int depth) {
    if (depth > 32) return;
    for (const Entry& e : d.List(s, n)) e.dir ? count(e.sector, e.size, depth + 1) : void(total += e.size);
  };
  count(d.root_sector(), d.root_size(), 0);
  uint64_t done = 0;
  task.Progress(0, (long long)total);
  std::vector<char> buf(1 << 20);
  std::string error;
  std::function<void(uint32_t, uint32_t, const fs::path&, int)> walk = [&](uint32_t s, uint32_t n,
                                                                         const fs::path& dir, int depth) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const Entry& e : d.List(s, n)) {
      if (!error.empty()) return;
      if (task.cancelled()) {
        error = "Cancelled.";
        return;
      }
      if (!SafeName(e.name)) continue;  // never write outside the output folder
      const fs::path path = dir / Utf8Path(e.name);
      if (e.dir) {
        if (depth < 32) walk(e.sector, e.size, path, depth + 1);
        continue;
      }
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out) {
        error = "Cannot write " + e.name + ". Is the disk full or the folder read-only?";
        return;
      }
      uint64_t left = e.size, offset = d.base() + uint64_t(e.sector) * kSector;
      while (left) {
        const size_t chunk = size_t(std::min<uint64_t>(left, buf.size()));
        if (!d.Read(offset, buf.data(), chunk)) {
          error = "The disc image ended early while reading " + e.name + ".";
          return;
        }
        out.write(buf.data(), std::streamsize(chunk));
        offset += chunk;
        left -= chunk;
        done += chunk;
        task.Progress((long long)done, (long long)total);
        if (task.cancelled()) {
          error = "Cancelled.";
          return;
        }
      }
    }
  };
  walk(d.root_sector(), d.root_size(), Utf8Path(utf8_out_dir), 0);
  return error;
}

}  // namespace trg
