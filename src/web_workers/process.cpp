#ifndef _WIN32
#include "web_workers/process.h"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace ruyijs::web_workers {
namespace {
pid_t PidOf(const void* slot) { return static_cast<pid_t>(reinterpret_cast<intptr_t>(slot)); }

bool Alive(pid_t pid) {
  if (pid <= 0) return false;
  siginfo_t info{};
  if (waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0) return false;
  return info.si_pid == 0;
}
}  // namespace

Process::~Process() { Stop(); }
bool Process::Running() const { return Alive(PidOf(process_)); }
unsigned Process::id() const {
  const pid_t pid = PidOf(process_);
  return pid > 0 ? static_cast<unsigned>(pid) : 0;
}
bool Process::Queue(std::string frame) { return Running() && pipe_.Queue(std::move(frame)); }
PipeResult Process::Poll(std::string* frame, bool* received) {
  const auto result = pipe_.Poll(frame, received);
  if (!*received && result == PipeResult::Pending && !pipe_.connected() && !Running())
    return PipeResult::Closed;
  return result;
}
bool Process::Stop() {
  const pid_t pid = PidOf(process_);
  const pid_t group = PidOf(job_);
  process_ = nullptr;
  job_ = nullptr;
  if (group > 0) kill(-group, SIGKILL);
  bool stopped = true;
  if (pid > 0) {
    kill(pid, SIGKILL);
    int status = 0;
    stopped = false;
    for (int spin = 0; spin < 500; ++spin) {
      const pid_t got = waitpid(pid, &status, WNOHANG);
      if (got == pid || (got < 0 && errno == ECHILD)) { stopped = true; break; }
      usleep(10000);
    }
  }
  pipe_.Close();
  return stopped;
}
}
#else
#include "web_workers/process.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ruyijs::web_workers {
Process::~Process() { Stop(); }
bool Process::Running() const {
  return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}
unsigned Process::id() const { return process_ ? GetProcessId(process_) : 0; }
bool Process::Queue(std::string frame) {
  return Running() && pipe_.Queue(std::move(frame));
}
PipeResult Process::Poll(std::string* frame, bool* received) {
  const auto result = pipe_.Poll(frame, received);
  // Drain already-completed messages before noticing an exited child. A child
  // can exit before connecting, in which case accept would remain pending.
  if (!*received && result == PipeResult::Pending && !pipe_.connected() && !Running())
    return PipeResult::Closed;
  return result;
}
bool Process::Stop() {
  // Closing the Job also covers descendants when its immediate child already
  // exited. The direct termination covers a failed Job assignment at startup.
  if (job_) { CloseHandle(job_);job_ = nullptr; }
  bool stopped = true;
  if (process_) {
    if (Running()) TerminateProcess(process_, 1);
    stopped = WaitForSingleObject(process_, 5000) == WAIT_OBJECT_0;
    CloseHandle(process_);process_ = nullptr;
  }
  pipe_.Close();
  return stopped;
}
}

#endif
