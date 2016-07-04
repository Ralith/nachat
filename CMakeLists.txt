cmake_minimum_required(VERSION 3.1)

project(native-chat CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-keyword-macro -Wno-missing-braces -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=undefined")

# Qt
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(GSL_PATH "${PROJECT_SOURCE_DIR}/deps/GSL")

include_directories("${GSL_PATH}/include")

add_subdirectory(src)
