#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::input {

// Message type constants for SecureChannel frames carrying input events,
// continuing the numbering after discovery::kMsgMonitorList (= 1).
constexpr uint8_t kMsgMouseAbsolute = 2;  // int32 x, int32 y — warp + continuous streaming, same message
constexpr uint8_t kMsgMouseButton = 3;   // uint8 button, uint8 down
constexpr uint8_t kMsgMouseWheel = 4;    // int16 delta
constexpr uint8_t kMsgKey = 5;           // uint32 vk, uint32 scancode, uint8 down, uint8 extended
constexpr uint8_t kMsgModifierSync = 6;  // uint8 mask

std::vector<uint8_t> encode_mouse_absolute(int32_t x, int32_t y);
bool decode_mouse_absolute(const uint8_t* data, size_t len, int32_t& x, int32_t& y);

std::vector<uint8_t> encode_mouse_button(uint8_t button, bool down);
bool decode_mouse_button(const uint8_t* data, size_t len, uint8_t& button, bool& down);

std::vector<uint8_t> encode_mouse_wheel(int16_t delta);
bool decode_mouse_wheel(const uint8_t* data, size_t len, int16_t& delta);

std::vector<uint8_t> encode_key(uint32_t vk, uint32_t scancode, bool down, bool extended);
bool decode_key(const uint8_t* data, size_t len, uint32_t& vk, uint32_t& scancode, bool& down, bool& extended);

std::vector<uint8_t> encode_modifier_sync(uint8_t mask);
bool decode_modifier_sync(const uint8_t* data, size_t len, uint8_t& mask);

}  // namespace nockvm::input
