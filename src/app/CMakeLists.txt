include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.5.1
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui PUBLIC glfw)

set(NOCKVM_APP_COMMON_SOURCES main.cpp ui.cpp input_pump.cpp audio_pump.cpp auto_connect_pump.cpp quit.cpp clipboard_pump.cpp telemetry_pump.cpp)

if(WIN32)
  # WIN32 flag drops the console subsystem so the app runs windowed-only;
  # the tray icon (src/app/tray.cpp) becomes the only way to reach it once
  # the main window is hidden. app.rc embeds res/icons/NoCapKVM.ico as both
  # the tray icon and the .exe's own Explorer/taskbar icon.
  add_executable(nockvm_app WIN32 ${NOCKVM_APP_COMMON_SOURCES} tray.cpp app.rc)
else()
  # tray_linux.cpp implements the same tray.h API as a StatusNotifierItem
  # D-Bus service; ico_decode.cpp reads the installed icon for its
  # IconPixmap property (same source image the Windows .rc embeds).
  add_executable(nockvm_app ${NOCKVM_APP_COMMON_SOURCES} tray_linux.cpp ico_decode.cpp icon_path.cpp)
endif()
target_include_directories(nockvm_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nockvm_app PRIVATE nockvm_common nockvm_discovery nockvm_topology nockvm_input nockvm_audio nockvm_clipboard nockvm_telemetry imgui glfw)

if(WIN32)
  target_link_libraries(nockvm_app PRIVATE opengl32 shell32)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(nockvm_app PRIVATE OpenGL::GL)

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
  target_link_libraries(nockvm_app PRIVATE PkgConfig::DBUS)

  # main.cpp calls XInitThreads() directly (see its own comment for why) --
  # nockvm_display/nockvm_clipboard both already require X11 but keep its
  # include dir PRIVATE to themselves, so it doesn't propagate here.
  find_package(X11 REQUIRED)
  target_include_directories(nockvm_app PRIVATE ${X11_INCLUDE_DIR})

  include(GNUInstallDirs)
  set(NOCKVM_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../res/icons/NoCapKVM.ico")
  set(NOCKVM_DESKTOP_ICON_PATH "${CMAKE_INSTALL_FULL_DATADIR}/nockvm/NoCapKVM.ico")
  target_compile_definitions(nockvm_app PRIVATE NOCKVM_INSTALLED_ICON_PATH="${NOCKVM_DESKTOP_ICON_PATH}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../res/linux/nockvm.desktop.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/nockvm.desktop" @ONLY)
  add_custom_command(TARGET nockvm_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NOCKVM_ICON_SOURCE}" "$<TARGET_FILE_DIR:nockvm_app>/NoCapKVM.ico")
  install(TARGETS nockvm_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(FILES "${NOCKVM_ICON_SOURCE}" DESTINATION "${CMAKE_INSTALL_DATADIR}/nockvm")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/nockvm.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
