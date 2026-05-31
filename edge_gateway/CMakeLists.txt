cmake_minimum_required(VERSION 3.10)

project(edge_gateway CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(edge_gateway_demo
    src/main.cpp
)

target_compile_options(edge_gateway_demo PRIVATE
    -Wall
    -Wextra
)

install(TARGETS edge_gateway_demo
    RUNTIME DESTINATION bin
)
