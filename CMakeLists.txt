cmake_minimum_required(VERSION 3.10)
project(LogisticsOptimizationSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(include)

add_executable(LogisticsApp
    src/main.cpp
    src/routing.cpp
    src/allocation.cpp
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(LogisticsApp Threads::Threads)
