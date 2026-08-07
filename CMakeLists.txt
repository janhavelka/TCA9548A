find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/scripts/generate_version.py" check
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  RESULT_VARIABLE TCA9548A_VERSION_RESULT
  OUTPUT_VARIABLE TCA9548A_VERSION_OUTPUT
  ERROR_VARIABLE TCA9548A_VERSION_ERROR
)

if(NOT TCA9548A_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR
    "TCA9548A generated version metadata is out of date: "
    "${TCA9548A_VERSION_OUTPUT} ${TCA9548A_VERSION_ERROR}")
endif()

idf_component_register(
  SRCS "src/TCA9548A.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
