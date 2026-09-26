# Builds and runs the unit tests for the engine-agnostic gameplay core (Source/Amplitude/Core).
# The Unreal layer is built by Unreal Build Tool; this project only needs a C++20 compiler:
#
#   cmake -S Tests -B Intermediate/CoreTests && cmake --build Intermediate/CoreTests && ctest --test-dir Intermediate/CoreTests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(AmplitudeCoreTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AMP_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Amplitude)
file(GLOB AMP_CORE_SOURCES CONFIGURE_DEPENDS ${AMP_SOURCE_ROOT}/Core/*.cpp)
file(GLOB AMP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(AmplitudeCoreTests ${AMP_CORE_SOURCES} ${AMP_TEST_SOURCES})
target_include_directories(AmplitudeCoreTests PRIVATE ${AMP_SOURCE_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})

# Mirror Unreal's strictness: shadowed variables are errors in UE5 builds.
if(MSVC)
	target_compile_options(AmplitudeCoreTests PRIVATE /W4 /WX /permissive-)
else()
	target_compile_options(AmplitudeCoreTests PRIVATE -Wall -Wextra -Wshadow -Wfloat-conversion -Werror -fno-exceptions)
endif()

enable_testing()
add_test(NAME AmplitudeCoreTests COMMAND AmplitudeCoreTests)
