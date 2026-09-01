#pragma once
#include "nockvm/discovery/noise_primitives.h"

namespace nockvm::discovery {

// Loads the persisted X25519 identity keypair, generating and storing one
// on first run. Honors the same NOCKVM_HOME override as get_or_create_device_id().
Keypair get_or_create_static_keypair();

}  // namespace nockvm::discovery
