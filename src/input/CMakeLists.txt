add_library(nockvm_input STATIC
  hook.cpp
  inject.cpp
  protocol.cpp
)
target_include_directories(nockvm_input PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_input PUBLIC nockvm_common)

if(WIN32)
  target_link_libraries(nockvm_input PUBLIC user32)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBEVDEV REQUIRED libevdev)
  target_include_directories(nockvm_input PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
  target_link_libraries(nockvm_input PUBLIC ${LIBEVDEV_LIBRARIES})
  target_link_directories(nockvm_input PRIVATE ${LIBEVDEV_LIBRARY_DIRS})
endif()
