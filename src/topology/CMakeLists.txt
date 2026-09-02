add_library(nockvm_topology STATIC
  arrangement.cpp
  crossing.cpp
)
target_include_directories(nockvm_topology PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_topology PUBLIC nockvm_common nockvm_display)
