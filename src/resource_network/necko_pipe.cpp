#ifndef _WIN32
#include "resource_network/necko_pipe.h"

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace ruyijs::resource_network {
DWORD NeckoRemaining(ULONGLONG deadline) {
  const ULONGLONG now = GetTickCount64();
  if (now >= deadline) return 0;
  const ULONGLONG left = deadline - now;
  return left > 0x7fffffff ? 0x7fffffff : DWORD(left);
}
bool NeckoPipe::Open(bool parentWrites) {
  int ends[2] = {-1, -1};
  if (pipe(ends) != 0) return false;
  const int parentFd = parentWrites ? ends[1] : ends[0];
  const int childFd = parentWrites ? ends[0] : ends[1];
  if (fcntl(parentFd, F_SETFD, FD_CLOEXEC) != 0 ||
      fcntl(parentFd, F_SETFL, O_NONBLOCK) != 0) {
    close(ends[0]);
    close(ends[1]);
    return false;
  }
  parent_.Reset(parentFd);
  child_.Reset(childFd);
  return true;
}
bool NeckoPipe::Transfer(void* data, uint32_t size, bool writing, ULONGLONG deadline) {
  auto* bytes = static_cast<char*>(data);
  uint32_t done = 0;
  while (done < size) {
    const DWORD remain = NeckoRemaining(deadline);
    if (!remain) return false;
    pollfd ready{};
    ready.fd = parent_.Get();
    ready.events = writing ? POLLOUT : POLLIN;
    const int waited = poll(&ready, 1, int(remain));
    if (waited < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (!waited) return false;
    const uint32_t amount = size - done > 65536 ? 65536 : size - done;
    ssize_t moved = 0;
    do {
      moved = writing ? ::write(parent_.Get(), bytes + done, amount)
                      : ::read(parent_.Get(), bytes + done, amount);
    } while (moved < 0 && errno == EINTR);
    if (moved < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (moved <= 0 || uint32_t(moved) > amount) return false;
    done += uint32_t(moved);
  }
  return true;
}
bool NeckoPipe::Write(const void* data, uint32_t size, ULONGLONG deadline) {
  return Transfer(const_cast<void*>(data), size, true, deadline);
}
bool NeckoPipe::Read(void* data, uint32_t size, ULONGLONG deadline) {
  return Transfer(data, size, false, deadline);
}
}
#else
#include "resource_network/necko_pipe.h"
#include <string>

namespace ruyijs::resource_network {
DWORD NeckoRemaining(ULONGLONG deadline) {
  const auto now = GetTickCount64();
  return now >= deadline ? 0 : DWORD(deadline - now);
}
bool NeckoPipe::Open(bool parentWrites) {
  static LONG sequence = 0;
  const auto name = L"\\\\.\\pipe\\ruyijs-necko-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(InterlockedIncrement(&sequence));
  parent_.Reset(CreateNamedPipeW(name.c_str(),
      (parentWrites ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND) |
          FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1, 65536, 65536, 0, nullptr));
  if (!parent_) return false;
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  child_.Reset(CreateFileW(name.c_str(), parentWrites ? GENERIC_READ : GENERIC_WRITE,
      0, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  event_.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!child_ || !event_) return false;
  OVERLAPPED connected{};
  connected.hEvent = event_.Get();
  // The local client was opened first, so connect completes immediately.
  if (ConnectNamedPipe(parent_.Get(), &connected)) return true;
  const DWORD error = GetLastError();
  if (error == ERROR_IO_PENDING) {
    DWORD ignored = 0;
    CancelIoEx(parent_.Get(), &connected);
    GetOverlappedResult(parent_.Get(), &connected, &ignored, TRUE);
  }
  return error == ERROR_PIPE_CONNECTED;
}
bool NeckoPipe::Transfer(void* data, uint32_t size, bool writing, ULONGLONG deadline) {
  auto* bytes = static_cast<char*>(data);
  uint32_t done = 0;
  while (done < size) {
    if (!NeckoRemaining(deadline)) return false;
    OVERLAPPED io{};
    io.hEvent = event_.Get();
    if (!ResetEvent(io.hEvent)) return false;
    DWORD moved = 0;
    const DWORD amount = size - done > 65536 ? 65536 : size - done;
    const BOOL ready = writing ?
        WriteFile(parent_.Get(), bytes + done, amount, &moved, &io) :
        ReadFile(parent_.Get(), bytes + done, amount, &moved, &io);
    if (!ready) {
      if (GetLastError() != ERROR_IO_PENDING) return false;
      if (WaitForSingleObject(io.hEvent, NeckoRemaining(deadline)) != WAIT_OBJECT_0) {
        CancelIoEx(parent_.Get(), &io);
        // Complete cancellation before the OVERLAPPED or caller buffer dies.
        GetOverlappedResult(parent_.Get(), &io, &moved, TRUE);
        return false;
      }
      if (!GetOverlappedResult(parent_.Get(), &io, &moved, FALSE)) return false;
    }
    if (!moved || moved > amount) return false;
    done += moved;
  }
  return true;
}
bool NeckoPipe::Write(const void* data, uint32_t size, ULONGLONG deadline) {
  return Transfer(const_cast<void*>(data), size, true, deadline);
}
bool NeckoPipe::Read(void* data, uint32_t size, ULONGLONG deadline) {
  return Transfer(data, size, false, deadline);
}
}

#endif
