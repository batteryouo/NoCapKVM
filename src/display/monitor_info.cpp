#include "nockvm/display/monitor_info.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#endif

namespace nockvm::display {

#ifdef _WIN32

namespace {

BOOL CALLBACK monitor_enum_proc(HMONITOR monitor, HDC, LPRECT, LPARAM user_data) {
  auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(user_data);

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return TRUE;

  MonitorInfo m;
  m.x = info.rcMonitor.left;
  m.y = info.rcMonitor.top;
  m.width = info.rcMonitor.right - info.rcMonitor.left;
  m.height = info.rcMonitor.bottom - info.rcMonitor.top;
  m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

  int len = WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, nullptr, 0, nullptr, nullptr);
  if (len > 0) {
    std::string name(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, name.data(), len, nullptr, nullptr);
    m.name = std::move(name);
  }

  out->push_back(std::move(m));
  return TRUE;
}

}  // namespace

std::vector<MonitorInfo> get_local_monitors() {
  std::vector<MonitorInfo> out;
  EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc, reinterpret_cast<LPARAM>(&out));
  return out;
}

#else

std::vector<MonitorInfo> get_local_monitors() {
  std::vector<MonitorInfo> out;

  Display* display = XOpenDisplay(nullptr);
  if (!display) return out;

  Window root = DefaultRootWindow(display);
  XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display, root);
  if (!resources) {
    XCloseDisplay(display);
    return out;
  }

  const RROutput primary_output = XRRGetOutputPrimary(display, root);

  for (int i = 0; i < resources->noutput; ++i) {
    XRROutputInfo* output_info = XRRGetOutputInfo(display, resources, resources->outputs[i]);
    if (!output_info) continue;

    if (output_info->connection == RR_Connected && output_info->crtc != 0) {
      XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display, resources, output_info->crtc);
      if (crtc_info) {
        MonitorInfo m;
        m.x = crtc_info->x;
        m.y = crtc_info->y;
        m.width = static_cast<int32_t>(crtc_info->width);
        m.height = static_cast<int32_t>(crtc_info->height);
        m.primary = resources->outputs[i] == primary_output;
        m.name.assign(output_info->name, static_cast<size_t>(output_info->nameLen));
        out.push_back(std::move(m));
        XRRFreeCrtcInfo(crtc_info);
      }
    }

    XRRFreeOutputInfo(output_info);
  }

  XRRFreeScreenResources(resources);
  XCloseDisplay(display);
  return out;
}

#endif

}  // namespace nockvm::display
