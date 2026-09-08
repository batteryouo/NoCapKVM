#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace nockvm::clipboard {

enum class ContentType : uint8_t { Text, Jpeg };

struct ClipboardContent {
  ContentType type = ContentType::Text;
  std::vector<uint8_t> data;  // UTF-8 bytes for Text, JPEG-encoded bytes for Jpeg

  bool operator==(const ClipboardContent& other) const { return type == other.type && data == other.data; }
  bool operator!=(const ClipboardContent& other) const { return !(*this == other); }
};

// Reads text as UTF-8 or images as JPEG; returns nullopt for unsupported or empty content.
std::optional<ClipboardContent> read_clipboard();

// Reports whether clipboard content changed since the previous successful check.
bool clipboard_changed();

// Writes content to the OS clipboard in a format native applications can paste.
void write_clipboard(const ClipboardContent& content);

// Services clipboard events; call every frame while the application is running.
void pump_events();

}  // namespace nockvm::clipboard
