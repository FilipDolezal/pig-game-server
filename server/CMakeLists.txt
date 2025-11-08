cmake_minimum_required(VERSION 3.10)
project(ups-sp-server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

include_directories(include)

find_package(Threads REQUIRED)

file(GLOB SRCS "src/*.c")

add_executable(server ${SRCS})

target_link_libraries(server Threads::Threads)

