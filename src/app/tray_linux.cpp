#ifndef _WIN32

#include "tray.h"

#include <dbus/dbus.h>

#include <GLFW/glfw3.h>

#include <optional>
#include <string>

#include "ico_decode.h"
#include "quit.h"

// StatusNotifierItem (SNI) is the Linux desktop-tray protocol KDE/most
// modern panels implement: a D-Bus service exposing a small fixed set of
// properties (icon, title, status) plus Activate()/ContextMenu() methods
// the panel calls on left/right click. Deliberately NOT implementing
// com.canonical.dbusmenu here -- that's a real dropdown-menu tree over
// D-Bus and would be a much larger surface for no functional gain here.
// Instead, right-click's ContextMenu() just quits directly (see quit.h),
// matching the two plain actions this app actually needs.
//
// Everything below runs off one non-blocking poll per frame (pump_tray(),
// called from main.cpp's loop) rather than a background event-loop thread
// -- so Activate/ContextMenu execute on the main thread and can call GLFW
// functions directly, with no cross-thread handoff to get wrong (unlike a
// threaded D-Bus event loop would need).
namespace nockvm::app {
namespace {

constexpr const char* kObjectPath = "/StatusNotifierItem";
constexpr const char* kInterface = "org.kde.StatusNotifierItem";
constexpr const char* kIconPath = "res/icons/NoCapKVM.ico";  // relative to the process's cwd

DBusConnection* g_conn = nullptr;
GLFWwindow* g_window = nullptr;
std::optional<DecodedIcon> g_icon;

void restore_window() {
  if (!g_window) return;
  glfwShowWindow(g_window);
  glfwFocusWindow(g_window);
}

const char* kIntrospectionXml =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\"><arg name=\"xml\" type=\"s\" direction=\"out\"/></method>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\">\n"
    "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"name\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"GetAll\">\n"
    "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"values\" type=\"a{sv}\" direction=\"out\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name=\"org.kde.StatusNotifierItem\">\n"
    "    <method name=\"Activate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>\n"
    "    <method name=\"SecondaryActivate\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>\n"
    "    <method name=\"ContextMenu\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/></method>\n"
    "    <method name=\"Scroll\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/></method>\n"
    "    <property name=\"Category\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Id\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Title\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
    "    <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>\n"
    "  </interface>\n"
    "</node>\n";

void append_icon_pixmap_array(DBusMessageIter* iter) {
  DBusMessageIter array_iter;
  dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(iiay)", &array_iter);
  if (g_icon) {
    DBusMessageIter struct_iter;
    dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
    dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &g_icon->width);
    dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &g_icon->height);
    DBusMessageIter byte_array_iter;
    dbus_message_iter_open_container(&struct_iter, DBUS_TYPE_ARRAY, "y", &byte_array_iter);
    const uint8_t* data_ptr = g_icon->argb32_be.data();
    const int byte_count = static_cast<int>(g_icon->argb32_be.size());
    dbus_message_iter_append_fixed_array(&byte_array_iter, DBUS_TYPE_BYTE, &data_ptr, byte_count);
    dbus_message_iter_close_container(&struct_iter, &byte_array_iter);
    dbus_message_iter_close_container(&array_iter, &struct_iter);
  }
  dbus_message_iter_close_container(iter, &array_iter);
}

void append_variant_string(DBusMessageIter* iter, const char* value) {
  DBusMessageIter variant_iter;
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
  dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &value);
  dbus_message_iter_close_container(iter, &variant_iter);
}

void append_variant_bool(DBusMessageIter* iter, bool value) {
  dbus_bool_t v = value ? TRUE : FALSE;
  DBusMessageIter variant_iter;
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &variant_iter);
  dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_BOOLEAN, &v);
  dbus_message_iter_close_container(iter, &variant_iter);
}

void append_variant_icon_pixmap(DBusMessageIter* iter) {
  DBusMessageIter variant_iter;
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant_iter);
  append_icon_pixmap_array(&variant_iter);
  dbus_message_iter_close_container(iter, &variant_iter);
}

void append_variant_tooltip(DBusMessageIter* iter) {
  DBusMessageIter variant_iter;
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &variant_iter);
  DBusMessageIter struct_iter;
  dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
  const char* empty = "";
  dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &empty);
  append_icon_pixmap_array(&struct_iter);
  const char* title = "NoCapKVM";
  dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &title);
  dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &empty);
  dbus_message_iter_close_container(&variant_iter, &struct_iter);
  dbus_message_iter_close_container(iter, &variant_iter);
}

constexpr const char* kPropertyNames[] = {"Category", "Id",         "Title",      "Status",
                                           "IconName", "IconPixmap", "ItemIsMenu", "ToolTip"};

bool append_property_variant(DBusMessageIter* iter, const std::string& name) {
  if (name == "Category") { append_variant_string(iter, "ApplicationStatus"); return true; }
  if (name == "Id") { append_variant_string(iter, "NoCapKVM"); return true; }
  if (name == "Title") { append_variant_string(iter, "NoCapKVM"); return true; }
  if (name == "Status") { append_variant_string(iter, "Active"); return true; }
  // Icon theme fallback name, used if IconPixmap decoding failed (e.g. the
  // .ico file was missing or only had PNG-compressed frames).
  if (name == "IconName") { append_variant_string(iter, g_icon ? "" : "utilities-terminal"); return true; }
  if (name == "IconPixmap") { append_variant_icon_pixmap(iter); return true; }
  if (name == "ItemIsMenu") { append_variant_bool(iter, false); return true; }
  if (name == "ToolTip") { append_variant_tooltip(iter); return true; }
  return false;
}

void send_empty_reply(DBusConnection* conn, DBusMessage* msg) {
  DBusMessage* reply = dbus_message_new_method_return(msg);
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
}

void handle_introspect(DBusConnection* conn, DBusMessage* msg) {
  DBusMessage* reply = dbus_message_new_method_return(msg);
  dbus_message_append_args(reply, DBUS_TYPE_STRING, &kIntrospectionXml, DBUS_TYPE_INVALID);
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
}

void handle_get(DBusConnection* conn, DBusMessage* msg) {
  DBusError err;
  dbus_error_init(&err);
  const char* interface_name = nullptr;
  const char* prop_name = nullptr;
  if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &interface_name, DBUS_TYPE_STRING, &prop_name,
                              DBUS_TYPE_INVALID)) {
    dbus_error_free(&err);
    return;
  }
  DBusMessage* reply = dbus_message_new_method_return(msg);
  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);
  if (!append_property_variant(&iter, prop_name)) {
    dbus_message_unref(reply);
    reply = dbus_message_new_error(msg, "org.freedesktop.DBus.Error.UnknownProperty", "no such property");
  }
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
}

void handle_get_all(DBusConnection* conn, DBusMessage* msg) {
  DBusMessage* reply = dbus_message_new_method_return(msg);
  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);
  DBusMessageIter dict_iter;
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);
  for (const char* name : kPropertyNames) {
    DBusMessageIter entry_iter;
    dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry_iter);
    dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &name);
    append_property_variant(&entry_iter, name);
    dbus_message_iter_close_container(&dict_iter, &entry_iter);
  }
  dbus_message_iter_close_container(&iter, &dict_iter);
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
}

DBusHandlerResult message_handler(DBusConnection* conn, DBusMessage* msg, void*) {
  if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
    handle_introspect(conn, msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
    handle_get_all(conn, msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
    handle_get(conn, msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_method_call(msg, kInterface, "Activate") ||
      dbus_message_is_method_call(msg, kInterface, "SecondaryActivate")) {
    restore_window();
    send_empty_reply(conn, msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_method_call(msg, kInterface, "ContextMenu")) {
    // No dropdown menu (see file comment) -- right-click just quits.
    request_quit();
    send_empty_reply(conn, msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (dbus_message_is_method_call(msg, kInterface, "Scroll")) {
    send_empty_reply(conn, msg);
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusObjectPathVTable g_vtable = {nullptr, message_handler, nullptr, nullptr, nullptr, nullptr};

}  // namespace

void install_tray(GLFWwindow* window) {
  g_window = window;
  g_icon = load_ico_as_argb32(kIconPath);

  DBusError err;
  dbus_error_init(&err);
  g_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (!g_conn) {
    dbus_error_free(&err);
    return;
  }
  dbus_connection_set_exit_on_disconnect(g_conn, FALSE);
  dbus_connection_register_object_path(g_conn, kObjectPath, &g_vtable, nullptr);

  // Best-effort registration with the panel's watcher: if it isn't running
  // yet (race at login) or absent entirely (no SNI-capable panel), the
  // call is simply ignored and the tray icon never appears -- the rest of
  // the app is unaffected either way.
  DBusMessage* msg = dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                    "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
  if (msg) {
    const char* path = kObjectPath;
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &path, DBUS_TYPE_INVALID);
    dbus_connection_send(g_conn, msg, nullptr);
    dbus_message_unref(msg);
  }
}

void uninstall_tray() {
  if (!g_conn) return;
  dbus_connection_unregister_object_path(g_conn, kObjectPath);
  dbus_connection_close(g_conn);
  dbus_connection_unref(g_conn);
  g_conn = nullptr;
  g_window = nullptr;
}

void pump_tray() {
  if (!g_conn) return;
  dbus_connection_read_write(g_conn, 0);  // non-blocking
  while (dbus_connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS) {
  }
}

}  // namespace nockvm::app

#endif
