cmake_minimum_required(VERSION 3.24)
project(keyrill_windows LANGUAGES C CXX)
if(NOT MSVC OR NOT WIN32)
  message(FATAL_ERROR "The proof requires the Windows SDK and MSVC")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
add_compile_options(/W4 /WX /EHsc /utf-8)
get_filename_component(ROOT "../.." ABSOLUTE)
include_directories("${ROOT}/include")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FINGERPRINT "${CMAKE_CURRENT_SOURCE_DIR}/build_fingerprint.py")
file(GLOB_RECURSE FINGERPRINT_MEMBERS CONFIGURE_DEPENDS
  "${ROOT}/include/*.h" "${ROOT}/crates/*.rs" "${ROOT}/crates/*.toml"
  "${ROOT}/crates/*.c" "${ROOT}/crates/*.h" "${ROOT}/crates/*.json" "${ROOT}/crates/*.jsonl"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.rs" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.toml" "${CMAKE_CURRENT_SOURCE_DIR}/*.def"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.ps1" "${CMAKE_CURRENT_SOURCE_DIR}/*.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.manifest" "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
  "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${FINGERPRINT}" --list
  OUTPUT_VARIABLE IDENTITY_PATHS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" IDENTITY_PATHS "${IDENTITY_PATHS}")
foreach(source IN LISTS IDENTITY_PATHS)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ROOT}/${source}")
endforeach()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${FINGERPRINT}"
  --header "${CMAKE_CURRENT_BINARY_DIR}/host_build.h" COMMAND_ERROR_IS_FATAL ANY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
add_compile_options(/volatile:ms)
execute_process(COMMAND cargo rustc --locked --offline --release -p keyrill-ffi -- --print native-static-libs
  WORKING_DIRECTORY "${ROOT}" RESULT_VARIABLE RUST_RESULT OUTPUT_VARIABLE RUST_OUTPUT ERROR_VARIABLE RUST_ERROR)
if(NOT RUST_RESULT EQUAL 0)
  message(FATAL_ERROR "Rust native library query failed: ${RUST_ERROR}")
endif()
string(REGEX MATCH "native-static-libs: ([^\r\n]+)" RUST_MATCH "${RUST_OUTPUT}${RUST_ERROR}")
set(RUST_LIBRARIES "${CMAKE_MATCH_1}")
if(NOT RUST_LIBRARIES MATCHES "/defaultlib:msvcrt")
  message(FATAL_ERROR "Expected Rust MSVC DLL runtime link contract")
endif()
separate_arguments(RUST_LIBRARIES WINDOWS_COMMAND "${RUST_LIBRARIES}")
list(FILTER RUST_LIBRARIES EXCLUDE REGEX "^/")
set(CARGO_PRODUCTION "${CMAKE_BINARY_DIR}/cargo-production")
set(CARGO_TEST "${CMAKE_BINARY_DIR}/cargo-test-api")
set(PACKAGED_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
add_custom_target(keyrill_windows_artifacts ALL
  COMMAND ${CMAKE_COMMAND} -E env "PYTHON=${Python3_EXECUTABLE}" "KEYRILL_ASAN=${KEYRILL_ASAN}"
    cargo build --locked --offline --release -p keyrill-windows --target-dir "${CARGO_PRODUCTION}"
  COMMAND ${CMAKE_COMMAND} -E env "PYTHON=${Python3_EXECUTABLE}" "KEYRILL_ASAN=${KEYRILL_ASAN}"
    cargo build --locked --offline --release -p keyrill-windows --features test-api --target-dir "${CARGO_TEST}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGED_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CARGO_PRODUCTION}/release/keyrill_windows.dll" "${PACKAGED_DIR}/KeyrillTsf.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CARGO_TEST}/release/keyrill_windows.dll" "${PACKAGED_DIR}/KeyrillTsfTest.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CARGO_PRODUCTION}/release/KeyrillRegister.exe" "${PACKAGED_DIR}/KeyrillRegister.exe"
  WORKING_DIRECTORY "${ROOT}" VERBATIM)
foreach(artifact KeyrillTsf KeyrillTsfTest)
  add_library(${artifact} SHARED IMPORTED GLOBAL)
  set_target_properties(${artifact} PROPERTIES IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/Release/${artifact}.dll")
  foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(${artifact} PROPERTIES
      IMPORTED_LOCATION_${configuration_upper} "${CMAKE_BINARY_DIR}/${configuration}/${artifact}.dll")
  endforeach()
  add_dependencies(${artifact} keyrill_windows_artifacts)
endforeach()
add_executable(keyrill_registration_tests register_tests.cpp)
target_link_libraries(keyrill_registration_tests PRIVATE ole32 oleaut32 advapi32 uuid)
add_executable(KeyrillProofEditor WIN32 editor.cpp proof_activation.manifest)
target_link_libraries(KeyrillProofEditor PRIVATE ole32 user32 uuid shell32)
add_dependencies(KeyrillProofEditor KeyrillTsf)
add_executable(KeyrillProofInspect inspect.cpp)
target_link_libraries(KeyrillProofInspect PRIVATE ole32 oleaut32 user32 uuid)
add_executable(KeyrillHostInspect host_inspect.cpp host_field.cpp host_observation.cpp host_probe.cpp guids.cpp)
target_link_libraries(KeyrillHostInspect PRIVATE ole32 oleaut32 user32 uuid advapi32)
add_executable(keyrill_host_probe_tests host_probe_tests.cpp host_observation.cpp host_probe.cpp guids.cpp)
target_link_libraries(keyrill_host_probe_tests PRIVATE ole32 oleaut32 user32 uuid advapi32)
add_executable(keyrill_host_threads_tests host_threads_tests.cpp)
target_link_libraries(keyrill_host_threads_tests PRIVATE advapi32)
add_executable(keyrill_host_inspect_options_tests host_inspect_options_tests.cpp host_field.cpp)
target_link_libraries(keyrill_host_inspect_options_tests PRIVATE ole32 oleaut32 user32 uuid)
add_executable(keyrill_host_field_tests host_field_tests.cpp host_field.cpp)
target_link_libraries(keyrill_host_field_tests PRIVATE ole32 oleaut32 user32 uuid)
add_executable(keyrill_routing_tests routing_tests.cpp)
add_executable(keyrill_windows_tests tests.cpp test_service.cpp test_keys.cpp test_native_field.cpp
  test_vocabulary.cpp guids.cpp host_observation.cpp host_probe.cpp)
set_target_properties(keyrill_windows_tests PROPERTIES OUTPUT_NAME KeyrillProofEditor
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
target_link_libraries(keyrill_windows_tests PRIVATE "${ROOT}/target/release/keyrill_ffi.lib"
  ${RUST_LIBRARIES} ole32 oleaut32 uuid user32 advapi32)
target_compile_definitions(keyrill_windows_tests PRIVATE KEYRILL_TEST_ARTIFACT
  "KEYRILL_TEST_DLL_LOCATION=L\"$<TARGET_FILE:KeyrillTsfTest>\"")
add_dependencies(keyrill_windows_tests keyrill_windows_artifacts)
add_executable(keyrill_dll_smoke dll_smoke.cpp)
target_link_libraries(keyrill_dll_smoke PRIVATE ole32 uuid)
add_executable(keyrill_activation_smoke activation_smoke.cpp proof_activation.manifest)
target_link_libraries(keyrill_activation_smoke PRIVATE ole32 uuid)
add_dependencies(keyrill_activation_smoke KeyrillTsf)
set(MISSING_DLL_DIR "${CMAKE_BINARY_DIR}/missing-adjacent/$<CONFIG>")
add_custom_command(TARGET keyrill_activation_smoke POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${MISSING_DLL_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:keyrill_activation_smoke>" "${MISSING_DLL_DIR}/")
enable_testing()
add_test(NAME host_routing_schedules COMMAND keyrill_routing_tests)
add_test(NAME host_diagnostic_mapping_schedules COMMAND keyrill_host_probe_tests)
add_test(NAME host_thread_startup_schedules COMMAND keyrill_host_threads_tests)
add_test(NAME host_inspector_argument_schedules COMMAND keyrill_host_inspect_options_tests)
add_test(NAME host_field_evidence_schedules COMMAND keyrill_host_field_tests)
add_test(NAME registration_preflight COMMAND keyrill_registration_tests $<TARGET_FILE:KeyrillTsfTest>)
add_test(NAME controlled_com_schedules COMMAND keyrill_windows_tests)
set_tests_properties(controlled_com_schedules PROPERTIES TIMEOUT 90)
add_test(NAME worker_filesystem_schedules COMMAND keyrill_windows_tests --runtime-files)
set_tests_properties(worker_filesystem_schedules PROPERTIES TIMEOUT 120)
add_test(NAME worker_actual_dll COMMAND keyrill_windows_tests --runtime-dll $<TARGET_FILE:KeyrillTsfTest>)
add_test(NAME worker_production_dll COMMAND keyrill_windows_tests --runtime-dll $<TARGET_FILE:KeyrillTsf>)
set_tests_properties(worker_actual_dll worker_production_dll PROPERTIES TIMEOUT 45)
add_test(NAME registration_free_factory COMMAND keyrill_dll_smoke $<TARGET_FILE:KeyrillTsf>)
add_test(NAME registration_free_activation COMMAND keyrill_activation_smoke)
add_test(NAME registration_free_activation_missing_dll
  COMMAND "${MISSING_DLL_DIR}/keyrill_activation_smoke.exe" --expect-missing)
add_executable(keyrill_abi_smoke "${ROOT}/crates/keyrill-ffi/tests/abi_smoke.c")
set_target_properties(keyrill_abi_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(keyrill_abi_smoke PRIVATE "${ROOT}/target/release/keyrill_ffi.lib" ${RUST_LIBRARIES})
add_test(NAME c11_abi_smoke COMMAND keyrill_abi_smoke)
add_subdirectory(rust/tests)
add_dependencies(keyrill_rust_boundary_tests keyrill_windows_artifacts)
foreach(schedule basic query-before query-after request-before request-after add-before add-after release-before release-after)
  add_test(NAME rust_com_boundary_${schedule}
    COMMAND keyrill_rust_boundary_tests $<TARGET_FILE:KeyrillTsfTest> ${schedule})
endforeach()
foreach(variant production test-api)
  set(decoder keyrill_diagnostic_decoder_${variant})
  add_executable(${decoder} rust/tests/diagnostics_layout.cpp host_probe.cpp guids.cpp)
  target_link_libraries(${decoder} PRIVATE ole32 oleaut32 user32 uuid advapi32)
  if(variant STREQUAL "test-api")
    target_compile_definitions(${decoder} PRIVATE KEYRILL_TEST_ARTIFACT)
    set(decoder_features --features test-api)
    set(decoder_cargo_dir "${CARGO_TEST}")
  else()
    set(decoder_features)
    set(decoder_cargo_dir "${CARGO_PRODUCTION}")
  endif()
  add_test(NAME rust_diagnostic_roundtrip_${variant}
    COMMAND ${CMAKE_COMMAND} -E env "PYTHON=${Python3_EXECUTABLE}" "KEYRILL_ASAN=${KEYRILL_ASAN}"
      "KEYRILL_DIAGNOSTIC_DECODER=$<TARGET_FILE:${decoder}>"
      cargo test --locked --offline -p keyrill-windows --lib ${decoder_features}
      --target-dir "${decoder_cargo_dir}"
      observation::tests::sdk_decoder_roundtrips_rust_payloads_through_logon_secured_mapping
      -- --exact --ignored)
  set_tests_properties(rust_diagnostic_roundtrip_${variant} PROPERTIES
    WORKING_DIRECTORY "${ROOT}" TIMEOUT 180 FAIL_REGULAR_EXPRESSION "running 0 tests")
endforeach()
if(KEYRILL_ASAN)
  foreach(target keyrill_windows_tests keyrill_dll_smoke keyrill_activation_smoke keyrill_registration_tests keyrill_routing_tests keyrill_host_probe_tests keyrill_host_threads_tests keyrill_host_inspect_options_tests keyrill_host_field_tests keyrill_rust_boundary_tests keyrill_diagnostic_decoder_production keyrill_diagnostic_decoder_test-api)
    target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
  endforeach()
endif()
