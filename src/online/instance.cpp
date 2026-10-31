#include "online/instance.h"

#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace ssf2t::online {

std::unique_ptr<InstanceLock> InstanceLock::TryAcquire(const std::filesystem::path& user_dir) {
  std::error_code ec;
  std::filesystem::create_directories(user_dir, ec);
  const std::filesystem::path file = user_dir / "online_instance.lock";
#if defined(_WIN32)
  // Share mode 0: a second open fails while this process lives, and the system releases it at exit.
  HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return nullptr;
  std::unique_ptr<InstanceLock> lock(new InstanceLock);
  lock->handle_ = reinterpret_cast<intptr_t>(h);
  return lock;
#else
  const int fd = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) return nullptr;
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    return nullptr;
  }
  std::unique_ptr<InstanceLock> lock(new InstanceLock);
  lock->handle_ = fd;
  return lock;
#endif
}

InstanceLock::~InstanceLock() {
  if (handle_ == -1) return;
#if defined(_WIN32)
  CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
  close(int(handle_));
#endif
}

}  // namespace ssf2t::online
