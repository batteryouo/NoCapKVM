#include "quit.h"

namespace nockvm::app {
namespace {
bool g_quit_requested = false;
}  // namespace

void request_quit() { g_quit_requested = true; }
bool quit_requested() { return g_quit_requested; }

}  // namespace nockvm::app
