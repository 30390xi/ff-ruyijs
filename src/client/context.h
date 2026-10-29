#ifndef RUYIJS_CLIENT_CONTEXT_H
#define RUYIJS_CLIENT_CONTEXT_H
#include <cstdint>
#include <mutex>
#include <atomic>
#include <string>
#include "ruyijs.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
using RuyijsPipe = HANDLE;
using RuyijsProcess = HANDLE;
#else
#include <sys/types.h>
using RuyijsPipe = int;
using RuyijsProcess = pid_t;
#endif

struct ruyijs_context {
#ifdef _WIN32
  RuyijsPipe pipe = INVALID_HANDLE_VALUE;
  RuyijsProcess process = nullptr;
#else
  RuyijsPipe pipe = -1;
  RuyijsProcess process = 0;
  std::string bound_name_;
#endif
  uint32_t timeout = 10000;
  bool active = false;
  std::atomic<bool> callback_active{false};
  ruyijs_callback callback = nullptr;
  void* callback_user = nullptr;
  std::mutex mutex;
  ~ruyijs_context();
  bool Connect(const char* shell, const char* mode, std::string* error);
  bool Exchange(const char* data, uint32_t length, std::string* response);
  bool Operation(bool writing, void* data, uint32_t length, uint32_t* count,
                 uint64_t deadline, bool connecting = false);
  bool Transfer(bool writing, void* data, uint32_t length, uint64_t deadline);
  bool WriteFrame(const char* data, uint32_t length, uint64_t deadline);
  bool ReadFrame(std::string* data, uint64_t deadline);
  void WaitProcess();
  void Stop();
  uint64_t Now() const;
};
constexpr uint32_t kRuyijsMaxFrame = 16 * 1024 * 1024;
#endif
