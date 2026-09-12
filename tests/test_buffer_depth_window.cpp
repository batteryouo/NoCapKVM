#include <cassert>
#include <chrono>
#include "buffer_depth_window.h"

using namespace nockvm::app;
using namespace std::chrono_literals;

int main() {
  const auto t0 = std::chrono::steady_clock::time_point(1000s);
  BufferDepthWindow window(10s);

  window.record(t0, 8);
  window.record(t0 + 5s, 24);
  assert(!window.ready());
  assert(window.maximum() == 24);

  window.record(t0 + 10s, 12);
  assert(window.ready());
  assert(window.maximum() == 24);

  window.record(t0 + 11s, 9);
  assert(window.ready());
  assert(window.maximum() == 24);

  window.record(t0 + 16s, 7);
  assert(window.ready());
  assert(window.maximum() == 12);

  window.reset();
  assert(!window.ready());
  assert(window.maximum() == 0);

  window.record(t0 + 30s, 30);
  window.record(t0 + 45s, 40);
  assert(!window.ready());
  assert(window.maximum() == 40);
  return 0;
}
