include_guard(GLOBAL)

# Fail the build before compiling this target if its contracts do not check.
# sources must enumerate its translation units; headers are parsed via includes.
function(clangsolv_enable target config)
  cmake_parse_arguments(SOLV "" "CHECKER" "SOURCES;OPTIONS;DEPENDS" ${ARGN})
  if(NOT SOLV_CHECKER OR NOT SOLV_SOURCES)
    message(FATAL_ERROR "clangsolv_enable requires CHECKER and SOURCES")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(tool_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  get_filename_component(config "${config}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/clangsolv-generated/${target}")
  set(header "${output_dir}/clangsolv_labels.hpp")
  add_custom_command(OUTPUT "${header}"
    COMMAND "${Python3_EXECUTABLE}" "${tool_root}/generator/generate_labels.py"
      --config "${config}" --output "${header}"
    DEPENDS "${config}" "${tool_root}/generator/generate_labels.py" VERBATIM)
  add_custom_target(${target}_clangsolv_labels DEPENDS "${header}")
  set(sources)
  foreach(source IN LISTS SOLV_SOURCES)
    get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND sources "${source}")
  endforeach()
  add_custom_target(${target}_clangsolv_check
    COMMAND "${SOLV_CHECKER}" --report "${CMAKE_CURRENT_BINARY_DIR}/${target}-clangsolv-report.json"
      ${sources} -- -std=c++17 "-I${tool_root}/library/include" "-I${output_dir}" ${SOLV_OPTIONS}
    DEPENDS "${header}" ${sources} ${SOLV_DEPENDS}
    COMMENT "ClangSOLV: checking value-state contracts" VERBATIM)
  add_dependencies(${target}_clangsolv_check ${target}_clangsolv_labels)
  add_dependencies(${target} ${target}_clangsolv_check)
  target_include_directories(${target} PRIVATE "${tool_root}/library/include" "${output_dir}")
  target_compile_features(${target} PRIVATE cxx_std_17)
endfunction()
