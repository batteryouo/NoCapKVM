#include "nockvm/telemetry/counters.h"

namespace nockvm::telemetry {

Counters& counters() {
  static Counters instance;
  return instance;
}

}  // namespace nockvm::telemetry
