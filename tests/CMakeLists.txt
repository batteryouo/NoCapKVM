add_executable(test_common test_common.cpp)
target_link_libraries(test_common PRIVATE nockvm_common)
add_test(NAME test_common COMMAND test_common)

add_executable(test_discovery test_discovery.cpp)
target_link_libraries(test_discovery PRIVATE nockvm_discovery)
add_test(NAME test_discovery COMMAND test_discovery)

add_executable(test_crypto test_crypto.cpp)
target_link_libraries(test_crypto PRIVATE nockvm_discovery)
add_test(NAME test_crypto COMMAND test_crypto)

add_executable(test_topology test_topology.cpp)
target_link_libraries(test_topology PRIVATE nockvm_topology)
add_test(NAME test_topology COMMAND test_topology)

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE nockvm_audio)
add_test(NAME test_audio COMMAND test_audio)

add_executable(test_clipboard test_clipboard.cpp)
target_link_libraries(test_clipboard PRIVATE nockvm_clipboard)
add_test(NAME test_clipboard COMMAND test_clipboard)

add_executable(test_telemetry test_telemetry.cpp)
target_link_libraries(test_telemetry PRIVATE nockvm_telemetry)
add_test(NAME test_telemetry COMMAND test_telemetry)

# retry_backoff.h is a header-only, dependency-free policy class living
# alongside src/app's other pump logic -- included directly rather than
# linking nockvm_app (an executable, not a library, and pulls in GLFW/
# ImGui/Windows deps this test has no need for).
add_executable(test_retry_backoff test_retry_backoff.cpp)
target_include_directories(test_retry_backoff PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
add_test(NAME test_retry_backoff COMMAND test_retry_backoff)
