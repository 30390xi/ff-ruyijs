#ifndef RUYIJS_NECKO_PIPE_H
#define RUYIJS_NECKO_PIPE_H
#ifdef _WIN32
#include <windows.h>
#include <cstdint>

namespace ruyijs::resource_network {
class NeckoHandle {
 public:
  explicit NeckoHandle(HANDLE value = nullptr) : value_(value) {}
  ~NeckoHandle() { Reset(); }
  NeckoHandle(const NeckoHandle&) = delete;
  NeckoHandle& operator=(const NeckoHandle&) = delete;
  HANDLE Get() const { return value_; }
  explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
  void Reset(HANDLE value = nullptr) {
    if (*this) CloseHandle(value_);
    value_ = value;
  }
 private:
  HANDLE value_;
};
DWORD NeckoRemaining(ULONGLONG deadline);
class NeckoPipe {
 public:
  bool Open(bool parentWrites);
  HANDLE Child() const { return child_.Get(); }
  void CloseChild() { child_.Reset(); }
  void CloseParent() { parent_.Reset(); }
  bool Write(const void* data, uint32_t size, ULONGLONG deadline);
  bool Read(void* data, uint32_t size, ULONGLONG deadline);
 private:
  bool Transfer(void* data, uint32_t size, bool writing, ULONGLONG deadline);
  NeckoHandle parent_, child_, event_;
};
}
#else
#include <chrono>
#include <cstdint>
#include <unistd.h>
using ULONGLONG = uint64_t;
using DWORD = uint32_t;
inline ULONGLONG GetTickCount64() {
  return ULONGLONG(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
namespace ruyijs::resource_network {
class NeckoHandle {
 public:
  explicit NeckoHandle(int value = -1) : value_(value) {}
  ~NeckoHandle() { Reset(); }
  NeckoHandle(const NeckoHandle&) = delete;
  NeckoHandle& operator=(const NeckoHandle&) = delete;
  int Get() const { return value_; }
  explicit operator bool() const { return value_ >= 0; }
  void Reset(int value = -1) {
    if (*this) close(value_);
    value_ = value;
  }
 private:
  int value_;
};
DWORD NeckoRemaining(ULONGLONG deadline);
class NeckoPipe {
 public:
  bool Open(bool parentWrites);
  int Child() const { return child_.Get(); }
  void CloseChild() { child_.Reset(); }
  void CloseParent() { parent_.Reset(); }
  bool Write(const void* data, uint32_t size, ULONGLONG deadline);
  bool Read(void* data, uint32_t size, ULONGLONG deadline);
 private:
  bool Transfer(void* data, uint32_t size, bool writing, ULONGLONG deadline);
  NeckoHandle parent_, child_, event_;
};
}
#endif
#endif
