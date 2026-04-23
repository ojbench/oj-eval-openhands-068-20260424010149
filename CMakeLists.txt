



cmake_minimum_required(VERSION 3.10)
project(TLSFAllocator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compiler options
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -pedantic")

# Set output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Create the executable
add_executable(code main.cpp allocator.cpp)




