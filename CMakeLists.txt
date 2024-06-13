cmake_minimum_required(VERSION 3.24)
project(klarity_sampler VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)

add_library(klarity_sampler SHARED src/media.cpp src/sampler.cpp)

target_include_directories(klarity_sampler PRIVATE include)

# OPENAL

SET(OPENAL_INCLUDE_PATH "include/openal")
target_include_directories(klarity_sampler PRIVATE ${OPENAL_INCLUDE_PATH})

SET(OPENAL_BIN_PATH "bin/openal")
target_link_directories(klarity_sampler PRIVATE ${OPENAL_BIN_PATH})

target_link_libraries(klarity_sampler PRIVATE soft_oal)