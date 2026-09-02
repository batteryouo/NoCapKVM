add_library(nockvm_input STATIC
  hook.cpp
  inject.cpp
  protocol.cpp
)
target_include_directories(nockvm_input PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_input PUBLIC nockvm_common)

if(WIN32)
  target_link_libraries(nockvm_input PUBLIC user32)
endif()
