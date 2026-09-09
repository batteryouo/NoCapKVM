add_library(nockvm_telemetry STATIC
  counters.cpp
  logger.cpp
  format.cpp
  process_stats.cpp
  telemetry.cpp
)
target_include_directories(nockvm_telemetry PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(nockvm_telemetry PUBLIC nockvm_common)
