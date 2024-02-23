cmake_minimum_required(VERSION 3.24)
project(klarity_audio VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)

add_library(klarity_audio SHARED src/audio.cpp)

target_include_directories(klarity_audio PRIVATE include)

# OPENAL

SET(OPENAL_INCLUDE_PATH "include/openal")
target_include_directories(klarity_audio PRIVATE ${OPENAL_INCLUDE_PATH})

SET(OPENAL_BIN_PATH "bin/openal")
target_link_directories(klarity_audio PRIVATE ${OPENAL_BIN_PATH})

target_link_libraries(klarity_audio PRIVATE soft_oal)