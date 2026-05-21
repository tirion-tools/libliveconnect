cmake_minimum_required(VERSION 3.16)
project(liveconnect
    VERSION 0.1.0
    DESCRIPTION "Thin SQL Server ODBC wrapper"
    LANGUAGES CXX
)

option(LIVECONNECT_BUILD_TESTS "Build libliveconnect tests" ON)
option(LIVECONNECT_INSTALL     "Install libliveconnect targets" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(ODBC REQUIRED)

include(FetchContent)
set(NANODBC_DISABLE_TESTS    ON  CACHE INTERNAL "")
set(NANODBC_DISABLE_EXAMPLES ON  CACHE INTERNAL "")
set(NANODBC_DISABLE_INSTALL  ON  CACHE INTERNAL "")
set(NANODBC_ENABLE_UNICODE   OFF CACHE INTERNAL "")
FetchContent_Declare(nanodbc
    GIT_REPOSITORY https://github.com/nanodbc/nanodbc.git
    GIT_TAG v2.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nanodbc)

add_library(liveconnect STATIC
    src/liveconnect.cpp
)
add_library(liveconnect::liveconnect ALIAS liveconnect)

target_include_directories(liveconnect
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(liveconnect PUBLIC ODBC::ODBC PRIVATE nanodbc)
target_compile_features(liveconnect PUBLIC cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liveconnect PRIVATE
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

if(LIVECONNECT_BUILD_TESTS)
    enable_testing()
    add_executable(liveconnect_test tests/test_liveconnect.cpp)
    target_link_libraries(liveconnect_test PRIVATE liveconnect)
    # Offline smoke test. Exercises odbc_quote only.
    add_test(NAME liveconnect_quote COMMAND liveconnect_test)
endif()

if(LIVECONNECT_INSTALL)
    include(GNUInstallDirs)
    # No EXPORT set. nanodbc is a private dependency fetched via
    # FetchContent and isn't in its own export set, so adding one
    # here makes CMake refuse to generate. Consumers using us as
    # a sibling add_subdirectory don't need the cmake config files
    # anyway.
    install(TARGETS liveconnect
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
