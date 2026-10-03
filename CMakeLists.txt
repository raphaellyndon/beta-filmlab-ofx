cmake_minimum_required(VERSION 3.16)
project(BETA_FilmLab_OFX CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFX headers (vendored in deps/openfx-main/include)
set(OFX_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/openfx-main/include")

add_library(BETA_FilmLab_OFX SHARED
    BETA_FilmLab.cpp
)
target_include_directories(BETA_FilmLab_OFX PRIVATE
    ${OFX_INCLUDE_DIR}
    .
)
target_compile_options(BETA_FilmLab_OFX PRIVATE -O2 -fvisibility=hidden -Wall)

# .ofx bundle layout: BETA_FilmLab.ofx/Contents/<platform>/BETA_FilmLab.ofx
if(APPLE)
    set(OFX_PLATFORM "MacOS")
elseif(WIN32)
    set(OFX_PLATFORM "Win64")
else()
    set(OFX_PLATFORM "Linux-x86-64")
endif()
set_target_properties(BETA_FilmLab_OFX PROPERTIES
    OUTPUT_NAME "BETA_FilmLab"
    SUFFIX ".ofx"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BETA_FilmLab.ofx/Contents/${OFX_PLATFORM}")
