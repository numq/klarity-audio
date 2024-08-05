cmake_minimum_required(VERSION 3.24)
project(klarity_sampler VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)

add_library(klarity_sampler SHARED src/media.cpp src/sampler.cpp)

target_include_directories(klarity_sampler PRIVATE include)

# PORTAUDIO

SET(PORTAUDIO_INCLUDE_PATH "include/portaudio")
target_include_directories(klarity_sampler PRIVATE ${PORTAUDIO_INCLUDE_PATH})

SET(PORTAUDIO_BIN_PATH "bin/portaudio")
target_link_directories(klarity_sampler PRIVATE ${PORTAUDIO_BIN_PATH})

target_link_libraries(klarity_sampler PRIVATE portaudio)