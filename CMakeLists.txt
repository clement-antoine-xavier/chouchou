cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(chouchou
    VERSION 0.1.0
    DESCRIPTION "chouchou"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_SANITIZERS "Enable Address/UB sanitizers" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(project_warnings INTERFACE)
if(MSVC)
    target_compile_options(project_warnings INTERFACE /W4)
else()
    target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(WARNINGS_AS_ERRORS)
        target_compile_options(project_warnings INTERFACE -Werror)
    endif()
endif()

if(ENABLE_SANITIZERS AND NOT MSVC)
    add_library(project_sanitizers INTERFACE)
    target_compile_options(project_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(project_sanitizers INTERFACE -fsanitize=address,undefined)
else()
    add_library(project_sanitizers INTERFACE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)

find_package(Boost REQUIRED COMPONENTS log)
find_package(Threads REQUIRED)

add_library(core STATIC
    src/greeter.cpp)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(core
    PUBLIC PkgConfig::LIBPQXX
    PRIVATE project_warnings project_sanitizers)

add_executable(chouchou src/main.cpp)
target_link_libraries(chouchou PRIVATE core Boost::headers Boost::log Threads::Threads)
target_compile_definitions(chouchou PRIVATE BOOST_LOG_DYN_LINK)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
