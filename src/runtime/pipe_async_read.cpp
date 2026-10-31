#include "runtime/pipe_async.h"

namespace ruyijs {
#ifdef _WIN32
PipeResult PipeRead(HANDLE pipe, PipeAsync& state) {
  if (state.received) return PipeResult::Ready;
  for (unsigned operation = 0; operation < 8; ++operation) {
    const size_t total = state.readingBody ? state.incoming.size() : sizeof(uint32_t);
    char* bytes = state.readingBody ? state.incoming.data() : reinterpret_cast<char*>(&state.readLength);
    DWORD count = 0;
    const auto result = PipeIo(pipe, state.read, false, bytes + state.readOffset,
                               DWORD(total - state.readOffset), &count);
    if (result != PipeResult::Ready) return result;
    state.readOffset += count;
    if (state.readOffset < total) continue;
    if (state.readingBody) {
      state.received = true;
      return PipeResult::Ready;
    }
    if (!Pipe::ValidFrameSize(state.readLength)) return PipeResult::Failed;
    try { state.incoming.resize(state.readLength); }
    catch (...) { return PipeResult::Failed; }
    state.readingBody = true; state.readOffset = 0;
  }
  return PipeResult::Pending;
}
#else
PipeResult PipeRead(void* pipe, PipeAsync& state) {
  if (state.received) return PipeResult::Ready;
  for (unsigned operation = 0; operation < 8; ++operation) {
    const size_t total = state.readingBody ? state.incoming.size() : sizeof(uint32_t);
    char* bytes = state.readingBody ? state.incoming.data() : reinterpret_cast<char*>(&state.readLength);
    unsigned count = 0;
    const auto result = PipeIo(pipe, state.read, false, bytes + state.readOffset,
                               static_cast<unsigned>(total - state.readOffset), &count);
    if (result != PipeResult::Ready) return result;
    state.readOffset += count;
    if (state.readOffset < total) continue;
    if (state.readingBody) {
      state.received = true;
      return PipeResult::Ready;
    }
    if (!Pipe::ValidFrameSize(state.readLength)) return PipeResult::Failed;
    try { state.incoming.resize(state.readLength); }
    catch (...) { return PipeResult::Failed; }
    state.readingBody = true; state.readOffset = 0;
  }
  return PipeResult::Pending;
}
#endif
}
