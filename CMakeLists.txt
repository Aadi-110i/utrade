cmake_minimum_required(VERSION 3.14)
project(kvstore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files
set(SOURCES
    src/main.cpp
    src/kvstore.cpp
    src/command_handler.cpp
    src/server.cpp
)

add_executable(kvstore ${SOURCES})

target_include_directories(kvstore PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Platform-specific linking
if(WIN32)
    target_link_libraries(kvstore PRIVATE ws2_32)
endif()

# Threads
find_package(Threads REQUIRED)
target_link_libraries(kvstore PRIVATE Threads::Threads)
