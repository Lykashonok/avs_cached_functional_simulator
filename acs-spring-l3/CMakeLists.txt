cmake_minimum_required(VERSION 3.14)
project(riscv_sim)

set(CMAKE_CXX_STANDARD 17)
SET(COMPILE_FLAGS "-Wall -Werr")

SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")

file(GLOB SRC
        "src/*.h"
        "src/*.cpp"
        )

add_executable(riscv_sim ${SRC})
