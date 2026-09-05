# ESP-IDF component. include/TCA9548A/Version.h is generated from library.json
# by scripts/generate_version.py; CI checks that it is up to date.
idf_component_register(
  SRCS "src/TCA9548A.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
