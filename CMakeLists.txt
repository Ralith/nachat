cmake_minimum_required(VERSION 3.1)

project(native-chat CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-keyword-macro -Wno-missing-braces -pthread")

# Qt
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(GSL_PATH "${PROJECT_SOURCE_DIR}/deps/GSL")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/")

# Options
set(BUILD_DEMOS ON CACHE BOOL "Whether to build demo executables for certain individual custom widgets")

include_directories("${GSL_PATH}/include")

add_subdirectory(src)
