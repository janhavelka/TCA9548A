idf_component_register(
  SRCS "main.cpp" "../../common/Tca9548aCli.cpp"
  INCLUDE_DIRS "." "../../.."
  REQUIRES TCA9548A esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_rom esp_system esp_timer freertos vfs
)

# Refresh identity on every build, including edits/commits without reconfigure.
# Only the example consumes this generated header; the portable core stays plain.
set(TCA9548A_IDENTITY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/Tca9548aBuildIdentity.h")
add_custom_target(tca9548a_example_identity
  COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../../scripts/generate_version.py"
          identity-header "${TCA9548A_IDENTITY_HEADER}"
  BYPRODUCTS "${TCA9548A_IDENTITY_HEADER}"
  VERBATIM
)
add_dependencies(${COMPONENT_LIB} tca9548a_example_identity)
target_compile_options(${COMPONENT_LIB} PRIVATE "-include" "${TCA9548A_IDENTITY_HEADER}")
