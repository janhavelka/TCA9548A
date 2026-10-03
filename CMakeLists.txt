cmake_minimum_required(VERSION 3.16)

# include/TCA9548A/Version.h is generated from library.json
# by scripts/generate_version.py; CI checks that it is up to date.
if(COMMAND idf_component_register)
  idf_component_register(
    SRCS "src/TCA9548A.cpp"
    INCLUDE_DIRS "include"
  )
  target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
else()
  project(TCA9548A LANGUAGES CXX)

  add_library(TCA9548A STATIC src/TCA9548A.cpp)
  add_library(TCA9548A::TCA9548A ALIAS TCA9548A)
  target_include_directories(TCA9548A PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_features(TCA9548A PUBLIC cxx_std_17)
endif()
