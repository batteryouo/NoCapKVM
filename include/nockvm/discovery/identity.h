#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "nockvm/discovery/types.h"

namespace nockvm::discovery {

// Reads the persisted device id from the config dir, generating and
// storing one on first run. Config dir resolution honors the NOCKVM_HOME
// env var override before falling back to the platform default.
uint64_t get_or_create_device_id();

std::string get_hostname();

// Session-memory for the role picked on the RoleSelect screen: nullopt on
// a genuinely first run (or if the file is missing/corrupt), otherwise
// whatever save_last_role() last wrote. The app uses this to skip
// RoleSelect and jump straight back into Discovery on startup -- the user
// picking a role again (after hitting Back) overwrites it, which is the
// only way to change it.
std::optional<Role> get_last_role();
void save_last_role(Role role);

}  // namespace nockvm::discovery
