#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace ssf2t::online {

// Held for the life of the process by the first instance that uses a user folder. A second instance on the same
// folder gets none and plays online under a temporary identity instead of sharing the first one's.
class InstanceLock {
 public:
  static std::unique_ptr<InstanceLock> TryAcquire(const std::filesystem::path& user_dir);
  ~InstanceLock();
  InstanceLock(const InstanceLock&) = delete;
  InstanceLock& operator=(const InstanceLock&) = delete;

 private:
  InstanceLock() = default;
  intptr_t handle_ = -1;
};

}  // namespace ssf2t::online
