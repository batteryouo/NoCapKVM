#include "nockvm/clipboard/clipboard.h"
#include <cstring>
#include <string>
#include "nockvm/clipboard/jpeg_codec.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <chrono>
#include <thread>
#endif

namespace nockvm::clipboard {
namespace {
constexpr int kJpegQuality = 85;
constexpr size_t kMaxJpegBytes = 6u * 1024 * 1024;  // safely under SecureChannel's 8MB frame cap
}  // namespace

#ifdef _WIN32

namespace {

// Pulls an 8-bit channel value out of a 32-bit pixel word given that
// channel's bitmask, for BI_BITFIELDS DIBs. Handles an arbitrary mask
// (not just the conventional 0xFF-per-channel layout) by shifting the
// masked bits down to the low end and then rescaling to 8 bits, since
// GDI+ (used by most modern screenshot/paste sources -- this is the
// format .NET's own Clipboard.SetImage produces, confirmed by testing
// against it directly) doesn't guarantee the 0x00FF0000/0x0000FF00/
// 0x000000FF convention BI_RGB pixels always use.
uint8_t extract_channel(uint32_t pixel, uint32_t mask) {
  if (mask == 0) return 0;
  const uint32_t masked = pixel & mask;
  int shift = 0;
  while (!((mask >> shift) & 1)) ++shift;
  int bits = 0;
  while ((mask >> (shift + bits)) & 1) ++bits;
  uint32_t value = masked >> shift;
  if (bits < 8) value <<= (8 - bits);
  else if (bits > 8) value >>= (bits - 8);
  return static_cast<uint8_t>(value & 0xFF);
}

// Caller must already hold the clipboard open (OpenClipboard succeeded).
std::optional<ClipboardContent> read_text() {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return std::nullopt;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (!h) return std::nullopt;
  const auto* wide = reinterpret_cast<const wchar_t*>(GlobalLock(h));
  if (!wide) return std::nullopt;

  std::optional<ClipboardContent> result;
  const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (len > 0) {
    // len (from cchWideChar == -1) includes the trailing NUL, and the
    // second call below is also told cchWideChar == -1, so it writes that
    // same NUL -- the destination buffer has to actually be len bytes, not
    // len-1, or this writes one byte past the end of the vector's
    // allocation. (This used to allocate len-1 directly and pass len as
    // cbMultiByte anyway: a real 1-byte heap buffer overflow on every text
    // clipboard read, corrupting the heap silently and crashing later on
    // an unrelated allocation -- reproduced locally by writing clipboard
    // text and immediately reading it back, which crashed consistently.)
    std::vector<uint8_t> utf8(static_cast<size_t>(len));
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, reinterpret_cast<char*>(utf8.data()), len, nullptr, nullptr);
    utf8.resize(static_cast<size_t>(len - 1));  // drop the trailing NUL now that it was safely written
    result = ClipboardContent{ContentType::Text, std::move(utf8)};
  }
  GlobalUnlock(h);
  return result;
}

// Caller must already hold the clipboard open. Handles the two DIB shapes
// a real copy operation actually produces: plain BI_RGB (24 or 32bpp,
// implicit BGR(A) byte order) and BI_BITFIELDS 32bpp with explicit masks
// (what GDI+ -- and so most modern screenshot/browser/paste sources --
// actually emits; verified by testing against .NET's own
// Clipboard.SetImage, which turned out to always use this, not BI_RGB).
// Paletted or RLE-compressed DIBs are rare from a real copy operation and
// are just skipped.
std::optional<ClipboardContent> read_image() {
  if (!IsClipboardFormatAvailable(CF_DIB)) return std::nullopt;
  HANDLE h = GetClipboardData(CF_DIB);
  if (!h) return std::nullopt;
  const auto* bih = reinterpret_cast<const BITMAPINFOHEADER*>(GlobalLock(h));
  if (!bih) return std::nullopt;

  std::optional<ClipboardContent> result;
  const bool is_rgb = bih->biCompression == BI_RGB && (bih->biBitCount == 24 || bih->biBitCount == 32);
  const bool is_bitfields32 = bih->biCompression == BI_BITFIELDS && bih->biBitCount == 32;

  if (is_rgb || is_bitfields32) {
    const int width = bih->biWidth;
    const bool top_down = bih->biHeight < 0;
    const int height = top_down ? -bih->biHeight : bih->biHeight;
    const int bytes_per_pixel = bih->biBitCount / 8;
    const size_t src_stride = ((static_cast<size_t>(width) * bytes_per_pixel + 3) / 4) * 4;
    const auto* header_end = reinterpret_cast<const uint8_t*>(bih) + bih->biSize;

    // BI_BITFIELDS DIBs carry three DWORD channel masks where a palette
    // would otherwise go, pushing the actual pixel data start back by 12
    // bytes.
    const uint8_t* pixels = header_end;
    uint32_t r_mask = 0x00FF0000, g_mask = 0x0000FF00, b_mask = 0x000000FF;
    if (is_bitfields32) {
      const auto* masks = reinterpret_cast<const uint32_t*>(header_end);
      r_mask = masks[0];
      g_mask = masks[1];
      b_mask = masks[2];
      pixels = header_end + 12;
    }

    std::vector<uint8_t> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (int y = 0; y < height; ++y) {
      // DIB rows are bottom-up unless biHeight is negative; flip either way
      // so the buffer handed to encode_jpeg is always top-down.
      const int src_row = top_down ? y : (height - 1 - y);
      const uint8_t* src = pixels + static_cast<size_t>(src_row) * src_stride;
      uint8_t* dst = rgb.data() + static_cast<size_t>(y) * width * 3;
      for (int x = 0; x < width; ++x) {
        if (is_bitfields32) {
          uint32_t pixel;
          std::memcpy(&pixel, src + x * 4, 4);
          dst[x * 3 + 0] = extract_channel(pixel, r_mask);
          dst[x * 3 + 1] = extract_channel(pixel, g_mask);
          dst[x * 3 + 2] = extract_channel(pixel, b_mask);
        } else {
          // Plain BI_RGB pixel order is BGR(A).
          dst[x * 3 + 0] = src[x * bytes_per_pixel + 2];
          dst[x * 3 + 1] = src[x * bytes_per_pixel + 1];
          dst[x * 3 + 2] = src[x * bytes_per_pixel + 0];
        }
      }
    }

    std::vector<uint8_t> jpeg = encode_jpeg(rgb.data(), width, height, kJpegQuality, kMaxJpegBytes);
    if (!jpeg.empty()) result = ClipboardContent{ContentType::Jpeg, std::move(jpeg)};
  }

  GlobalUnlock(h);
  return result;
}

void write_text(const std::vector<uint8_t>& utf8) {
  const std::string text(utf8.begin(), utf8.end());
  const int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (wide_len <= 0) return;

  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wide_len) * sizeof(wchar_t));
  if (!mem) return;
  auto* dst = reinterpret_cast<wchar_t*>(GlobalLock(mem));
  if (!dst) {
    GlobalFree(mem);
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, dst, wide_len);
  GlobalUnlock(mem);

  if (!OpenClipboard(nullptr)) {
    GlobalFree(mem);
    return;
  }
  EmptyClipboard();
  // SetClipboardData transfers ownership of mem to the system on success --
  // GlobalFree it ourselves only if that handoff never happened.
  if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
  CloseClipboard();
}

void write_image(const std::vector<uint8_t>& jpeg) {
  std::vector<uint8_t> rgb;
  int width = 0, height = 0;
  if (!decode_jpeg(jpeg.data(), jpeg.size(), rgb, width, height)) return;

  const size_t dst_stride = ((static_cast<size_t>(width) * 3 + 3) / 4) * 4;
  const size_t image_size = dst_stride * static_cast<size_t>(height);
  const size_t total = sizeof(BITMAPINFOHEADER) + image_size;

  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, total);
  if (!mem) return;
  auto* base = reinterpret_cast<uint8_t*>(GlobalLock(mem));
  if (!base) {
    GlobalFree(mem);
    return;
  }

  auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(base);
  *bih = BITMAPINFOHEADER{};
  bih->biSize = sizeof(BITMAPINFOHEADER);
  bih->biWidth = width;
  bih->biHeight = height;  // positive -- bottom-up, maximum compatibility with older apps
  bih->biPlanes = 1;
  bih->biBitCount = 24;
  bih->biCompression = BI_RGB;
  bih->biSizeImage = static_cast<DWORD>(image_size);

  uint8_t* pixels = base + bih->biSize;
  for (int y = 0; y < height; ++y) {
    const uint8_t* src = rgb.data() + static_cast<size_t>(y) * width * 3;  // rgb is top-down
    uint8_t* dst = pixels + static_cast<size_t>(height - 1 - y) * dst_stride;  // DIB storage is bottom-up
    for (int x = 0; x < width; ++x) {
      dst[x * 3 + 0] = src[x * 3 + 2];  // RGB -> BGR
      dst[x * 3 + 1] = src[x * 3 + 1];
      dst[x * 3 + 2] = src[x * 3 + 0];
    }
  }
  GlobalUnlock(mem);

  if (!OpenClipboard(nullptr)) {
    GlobalFree(mem);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_DIB, mem)) GlobalFree(mem);
  CloseClipboard();
}

}  // namespace

std::optional<ClipboardContent> read_clipboard() {
  if (!OpenClipboard(nullptr)) return std::nullopt;
  std::optional<ClipboardContent> result = read_text();
  if (!result) result = read_image();
  CloseClipboard();
  return result;
}

void write_clipboard(const ClipboardContent& content) {
  if (content.type == ContentType::Text) {
    write_text(content.data);
  } else {
    write_image(content.data);
  }
}

// Windows clipboard ownership doesn't require an owning process to keep
// answering anything after SetClipboardData -- the system itself holds
// and serves the data. Nothing to service.
void pump_events() {}

#else  // X11

namespace {

// A single persistent connection + utility window, unlike
// nockvm::display::get_local_monitors()'s open-per-call pattern -- the
// window has to keep existing between calls to stay the CLIPBOARD
// selection owner, and losing that ownership the moment this function
// returns would defeat the point of write_clipboard() entirely.
struct X11State {
  Display* display = nullptr;
  Window window = 0;
  Atom clipboard = 0;
  Atom utf8_string = 0;
  Atom targets = 0;
  Atom image_png = 0;
  Atom image_jpeg = 0;
  Atom transfer_prop = 0;  // property used to receive data from XConvertSelection requests we make

  // What we're currently offering as CLIPBOARD owner, filled in by
  // write_clipboard() and served out of service_pending_events() whenever
  // another app sends a SelectionRequest. wire mirrors exactly what
  // read_clipboard() should report back for the "we are the owner"
  // fast path, so a local echo of our own remote-applied write is
  // recognized without any round trip.
  bool have_owned = false;
  ClipboardContent wire;
  std::string owned_text;
  std::vector<uint8_t> owned_png;
};

X11State& state() {
  static X11State s;
  if (!s.display) {
    s.display = XOpenDisplay(nullptr);
    if (!s.display) return s;
    s.window = XCreateSimpleWindow(s.display, DefaultRootWindow(s.display), 0, 0, 1, 1, 0, 0, 0);
    s.clipboard = XInternAtom(s.display, "CLIPBOARD", False);
    s.utf8_string = XInternAtom(s.display, "UTF8_STRING", False);
    s.targets = XInternAtom(s.display, "TARGETS", False);
    s.image_png = XInternAtom(s.display, "image/png", False);
    s.image_jpeg = XInternAtom(s.display, "image/jpeg", False);
    s.transfer_prop = XInternAtom(s.display, "NOCKVM_CLIPBOARD_XFER", False);
  }
  return s;
}

void send_selection_notify(X11State& s, const XSelectionRequestEvent& req, Atom property) {
  XSelectionEvent notify{};
  notify.type = SelectionNotify;
  notify.display = req.display;
  notify.requestor = req.requestor;
  notify.selection = req.selection;
  notify.target = req.target;
  notify.property = property;
  notify.time = req.time;
  XSendEvent(s.display, req.requestor, False, NoEventMask, reinterpret_cast<XEvent*>(&notify));
}

void handle_selection_request(X11State& s, const XSelectionRequestEvent& req) {
  if (req.target == s.targets) {
    Atom offered[3];
    int count = 0;
    if (s.have_owned) {
      if (s.wire.type == ContentType::Text) {
        offered[count++] = s.utf8_string;
      } else {
        offered[count++] = s.image_png;
        offered[count++] = s.image_jpeg;
      }
    }
    XChangeProperty(s.display, req.requestor, req.property, XA_ATOM, 32, PropModeReplace,
                     reinterpret_cast<unsigned char*>(offered), count);
    send_selection_notify(s, req, req.property);
    return;
  }

  const unsigned char* data = nullptr;
  int len = 0;
  if (s.have_owned && s.wire.type == ContentType::Text &&
      (req.target == s.utf8_string || req.target == XA_STRING)) {
    data = reinterpret_cast<const unsigned char*>(s.owned_text.data());
    len = static_cast<int>(s.owned_text.size());
  } else if (s.have_owned && s.wire.type == ContentType::Jpeg && req.target == s.image_png) {
    data = s.owned_png.data();
    len = static_cast<int>(s.owned_png.size());
  } else if (s.have_owned && s.wire.type == ContentType::Jpeg && req.target == s.image_jpeg) {
    data = s.wire.data.data();
    len = static_cast<int>(s.wire.data.size());
  } else {
    send_selection_notify(s, req, None);  // unsupported target -- refuse per ICCCM
    return;
  }

  XChangeProperty(s.display, req.requestor, req.property, req.target, 8, PropModeReplace, data, len);
  send_selection_notify(s, req, req.property);
}

// Services whatever's queued on the X connection right now -- primarily
// SelectionRequest from other apps wanting to paste, since nothing else in
// this process runs a continuous X event loop. Called at the top of both
// read_clipboard() and write_clipboard(), which -- driven by
// app/clipboard_pump.cpp's ~1x/sec poll -- bounds how long another app
// might wait to paste our clipboard content to about that same interval.
void service_pending_events(X11State& s) {
  while (XPending(s.display) > 0) {
    XEvent event;
    XNextEvent(s.display, &event);
    if (event.type == SelectionRequest) {
      handle_selection_request(s, event.xselectionrequest);
    } else if (event.type == SelectionClear) {
      s.have_owned = false;
      s.owned_text.clear();
      s.owned_png.clear();
    }
  }
}

// Requests `target` from the current CLIPBOARD owner and waits (bounded)
// for the reply. Returns false if nothing arrived in time or the owner
// refused (property left as None).
bool convert_selection(X11State& s, Atom target, std::vector<uint8_t>& out) {
  XConvertSelection(s.display, s.clipboard, target, s.transfer_prop, s.window, CurrentTime);
  XFlush(s.display);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  while (std::chrono::steady_clock::now() < deadline) {
    XEvent event;
    if (XCheckTypedWindowEvent(s.display, s.window, SelectionNotify, &event)) {
      if (event.xselection.property == None) return false;

      Atom actual_type;
      int actual_format;
      unsigned long item_count, bytes_after;
      unsigned char* prop_data = nullptr;
      if (XGetWindowProperty(s.display, s.window, s.transfer_prop, 0, ~0L, True, AnyPropertyType, &actual_type,
                              &actual_format, &item_count, &bytes_after, &prop_data) != Success) {
        return false;
      }
      if (!prop_data) return false;
      out.assign(prop_data, prop_data + item_count);
      XFree(prop_data);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

// Legacy STRING data is Latin-1, not UTF-8 -- our wire format is
// declared UTF-8 (write_text() on the Windows side decodes it as such),
// so a source that only answers STRING (not every terminal/app supports
// UTF8_STRING, especially older or more minimal ones) needs converting
// rather than being passed through byte-for-byte.
std::string latin1_to_utf8(const std::vector<uint8_t>& latin1) {
  std::string out;
  out.reserve(latin1.size());
  for (uint8_t b : latin1) {
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
    } else {
      out.push_back(static_cast<char>(0xC0 | (b >> 6)));
      out.push_back(static_cast<char>(0x80 | (b & 0x3F)));
    }
  }
  return out;
}

}  // namespace

std::optional<ClipboardContent> read_clipboard() {
  X11State& s = state();
  if (!s.display) return std::nullopt;
  service_pending_events(s);

  if (XGetSelectionOwner(s.display, s.clipboard) == s.window) {
    // We're the owner -- nobody else could have changed the selection
    // without first taking ownership away from us, so this is exactly
    // what we last offered, no round trip needed.
    return s.have_owned ? std::make_optional(s.wire) : std::nullopt;
  }

  std::vector<uint8_t> bytes;
  if (convert_selection(s, s.utf8_string, bytes)) return ClipboardContent{ContentType::Text, std::move(bytes)};

  // Fall back to the legacy target -- not every source answers
  // UTF8_STRING (plenty of terminals/apps only ever offer STRING), and
  // without this fallback those sources' copies would never be seen at
  // all rather than just being handled less richly.
  if (convert_selection(s, XA_STRING, bytes)) {
    const std::string utf8 = latin1_to_utf8(bytes);
    return ClipboardContent{ContentType::Text, std::vector<uint8_t>(utf8.begin(), utf8.end())};
  }

  if (convert_selection(s, s.image_png, bytes)) {
    std::vector<uint8_t> rgb;
    int width = 0, height = 0;
    // stb_image auto-detects the container format from its header --
    // decode_jpeg works on any format it understands, PNG included.
    if (!decode_jpeg(bytes.data(), bytes.size(), rgb, width, height)) return std::nullopt;
    std::vector<uint8_t> jpeg = encode_jpeg(rgb.data(), width, height, kJpegQuality, kMaxJpegBytes);
    if (jpeg.empty()) return std::nullopt;
    return ClipboardContent{ContentType::Jpeg, std::move(jpeg)};
  }

  return std::nullopt;
}

void write_clipboard(const ClipboardContent& content) {
  X11State& s = state();
  if (!s.display) return;
  service_pending_events(s);

  s.wire = content;
  s.have_owned = true;
  if (content.type == ContentType::Text) {
    s.owned_text.assign(content.data.begin(), content.data.end());
    s.owned_png.clear();
  } else {
    std::vector<uint8_t> rgb;
    int width = 0, height = 0;
    s.owned_text.clear();
    s.owned_png.clear();
    if (decode_jpeg(content.data.data(), content.data.size(), rgb, width, height)) {
      s.owned_png = encode_png(rgb.data(), width, height);
    }
  }

  XSetSelectionOwner(s.display, s.clipboard, s.window, CurrentTime);
  XFlush(s.display);
}

void pump_events() {
  X11State& s = state();
  if (!s.display) return;
  service_pending_events(s);
}

#endif

}  // namespace nockvm::clipboard
