cmake_minimum_required(VERSION 3.14)
project(LogisticsOptimizationSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Optimised build unless asked otherwise (password hashing is slow without it)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include)

add_executable(LogisticsApp
    src/main.cpp
    src/routing.cpp
    src/allocation.cpp
    src/db.cpp
    src/schema.cpp
    src/password.cpp
    src/seed.cpp
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
target_link_libraries(LogisticsApp Threads::Threads SQLite::SQLite3)
