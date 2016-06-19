cmake_minimum_required(VERSION 2.6)

project(streamhlp)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

link_directories(${CMAKE_BINARY_DIR}/lib)

add_definitions(-mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -g -std=gnu99 -funwind-tables -O3)

file(GLOB_RECURSE SOURCES src/*.c)

add_library(streamhlp SHARED ${SOURCES})

if(CMAKE_SHARED_LIBRARY_SUFFIX MATCHES ".so")
    set_target_properties(streamhlp PROPERTIES SUFFIX ".so.1")
endif()


