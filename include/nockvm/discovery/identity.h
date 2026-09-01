#pragma once
#include <cstdint>
#include <string>

namespace nockvm::discovery {

// Reads the persisted device id from the config dir, generating and
// storing one on first run. Config dir resolution honors the NOCKVM_HOME
// env var override before falling back to the platform default.
uint64_t get_or_create_device_id();

std::string get_hostname();

}  // namespace nockvm::discovery
