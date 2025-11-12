cmake_minimum_required(VERSION 3.31)
project(kres)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

find_package(xxHash 0.8.3 CONFIG QUIET)
if (NOT xxHash_FOUND)
    message(STATUS "xxHash not found or target missing. Fetching from source...")
    include(FetchContent)
    FetchContent_Declare(
            xxHash
            GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
            GIT_TAG v0.8.3
            SYSTEM
    )

    FetchContent_MakeAvailable(xxHash)
    FetchContent_GetProperties(xxHash   # for some reason cmake is unable to populate these itself here
            SOURCE_DIR xxHash_SOURCE_DIR
            BINARY_DIR xxHash_BINARY_DIR
    )

    option(BUILD_SHARED_LIBS "Build shared libs" OFF)
    set(XXHASH_BUILD_XXHSUM OFF)

    add_subdirectory(
            ${xxHash_SOURCE_DIR}/cmake_unofficial
            ${xxHash_BINARY_DIR}
            EXCLUDE_FROM_ALL
    )
endif ()

add_library(kres STATIC kres/main.cpp
        kres/main.h
        kres/hash/crc32.c
        kres/hash/crc32.h
        kres/utility.h
        kres/types.h)
target_link_libraries(kres PUBLIC xxHash::xxhash)
target_include_directories(kres INTERFACE include)

find_package(Catch2 QUIET)
if (NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2
            GIT_TAG v3.10.0
            SYSTEM
    )
    FetchContent_GetProperties(Catch2)
    if (NOT Catch2_POPULATED)
        set(FETCHCONTENT_QUIET NO)
        FetchContent_MakeAvailable(Catch2)
    endif ()
endif ()

add_executable(tests
        tests/read_write_archive.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain kres)
target_compile_definitions(tests PRIVATE CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}")

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(tests)
