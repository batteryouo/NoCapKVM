#include <cassert>
#include <cstring>
#include "nockvm/common.h"

int main() {
  assert(std::strlen(nockvm::kVersion) > 0);
  return 0;
}
