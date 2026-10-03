cmake_minimum_required(VERSION 3.16)
project(TCA9548AConsumer LANGUAGES CXX)

# The linked target must supply both the include path and C++17 requirement.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(../.. tca9548a)
add_executable(core_consumer compile_main.cpp)
target_link_libraries(core_consumer PRIVATE TCA9548A::TCA9548A)

enable_testing()
add_test(NAME core_consumer COMMAND core_consumer)
