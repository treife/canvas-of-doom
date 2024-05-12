cmake_minimum_required(VERSION 3.6)
project(canvas-of-doom)

add_executable(start start.cc ldr.cc)
target_compile_features(start PRIVATE cxx_std_20)

add_library(paintext SHARED paintext.cc ldr.cc)
target_compile_features(paintext PRIVATE cxx_std_20)

add_library(sdlcap SHARED sdlcap.cc ldr.cc)
target_compile_features(sdlcap PRIVATE cxx_std_20)

add_custom_command(
  TARGET start POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy
    "${CMAKE_SOURCE_DIR}/640x480.png"
    "$<TARGET_FILE_DIR:start>/640x480.png"
)