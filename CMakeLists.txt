cmake_minimum_required(VERSION 3.10)
project(Rule110Compiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

file(GLOB SOURCES "src/*.cpp")

# Main binary: exclude verify_main
list(FILTER SOURCES EXCLUDE REGEX "verify_main\\.cpp$")
add_executable(rule110c ${SOURCES})

# Verify binary: exclude main, include verify_main
file(GLOB VERIFY_SOURCES "src/*.cpp")
list(FILTER VERIFY_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
add_executable(verify ${VERIFY_SOURCES})

