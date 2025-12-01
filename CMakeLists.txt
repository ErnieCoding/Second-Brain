cmake_minimum_required(VERSION 3.16)
project(second-brain CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source
file(GLOB_RECURSE SOURCES "src/*.cpp")

# My headers
include_directories(include)

# Libgit2
include_directories("C:/msys64/mingw64/include")
link_directories("C:/msys64/mingw64/lib")

find_library(LIBGIT2 libgit2 PATHS "C:/msys64/mingw64/lib")

add_executable(second-brain ${SOURCES})

target_link_libraries(second-brain PRIVATE ${LIBGIT2})