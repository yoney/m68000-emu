include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
# Prevent GoogleTest from overriding compiler/linker options on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(cpu_test
    cpu_test.cpp
)

target_link_libraries(cpu_test
    PRIVATE
        m68000
        GTest::gtest_main
)

target_compile_features(cpu_test PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(cpu_test PRIVATE
        /W4
        /WX
        /permissive-
    )
else()
    target_compile_options(cpu_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Werror
    )
endif()

gtest_discover_tests(cpu_test)
