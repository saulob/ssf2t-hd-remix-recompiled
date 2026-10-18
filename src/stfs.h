#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace ssf2t::stfs {

enum class Status {
  kOk,
  kOpenFailed,
  kNotPackage,
  kUnsupported,
  kCorrupt,
  kUnsafePath,
  kWriteFailed,
  kCancelled,
};

struct Result {
  Status status = Status::kOk;
  std::string detail;
  bool ok() const { return status == Status::kOk; }
};

// Returns false to cancel the extraction.
using ProgressFn = std::function<bool(uint64_t done, uint64_t total)>;

// Reads a read-only STFS package (LIVE, PIRS or CON) whose files are stored in consecutive blocks,
// which is how Xbox Live Arcade packages are laid out.
class Package {
 public:
  Result Open(const std::filesystem::path& path);
  // Reads the file table and rejects any entry that could write outside the extraction root.
  Result LoadFiles();

  uint32_t title_id() const { return title_id_; }
  uint32_t content_type() const { return content_type_; }
  // Upper-case hexadecimal, 40 digits.
  std::string content_id_hex() const;
  // The English display name from the package metadata, empty when there is none.
  const std::u16string& display_name() const { return display_name_; }
  // Licence bits of the entries that have flags set, combined as the SDK's content manager does.
  uint32_t license_mask() const;
  uint64_t total_bytes() const { return total_bytes_; }
  size_t file_count() const { return file_count_; }

  // `root` must be an empty folder the caller owns.
  Result ExtractTo(const std::filesystem::path& root, const ProgressFn& progress);

 private:
  struct Entry {
    std::string path;
    uint32_t start_block = 0;
    uint32_t size = 0;
    bool directory = false;
  };

  bool ReadAt(uint64_t offset, void* out, size_t length);
  uint64_t BlockOffset(uint64_t block) const;
  std::u16string ReadEnglishName();

  std::ifstream in_;
  uint64_t file_size_ = 0;
  uint8_t header_[0x400] = {};
  uint32_t title_id_ = 0;
  uint32_t content_type_ = 0;
  std::array<uint8_t, 20> content_id_ = {};
  std::u16string display_name_;
  uint64_t base_ = 0;
  uint64_t total_bytes_ = 0;
  size_t file_count_ = 0;
  std::vector<Entry> entries_;
};

}  // namespace ssf2t::stfs
