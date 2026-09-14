cmake_minimum_required(VERSION 3.20)

project(
    m68000-emu
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(m68000
    src/cpu.cpp
)
add_library(m68000::m68000 ALIAS m68000)

target_include_directories(m68000
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(m68000 PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(m68000 PRIVATE
        /W4
        /WX
        /permissive-
    )
else()
    target_compile_options(m68000 PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Werror
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
