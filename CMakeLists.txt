cmake_minimum_required(VERSION 3.12)
project(argonOneUpLidMonitor VERSION 1.0.1 LANGUAGES CXX)

#--------------------------------------------------------------------------

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

#--------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod libgpiodcxx)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

#--------------------------------------------------------------------------

if (CMAKE_BUILD_TYPE STREQUAL "")
set(CMAKE_BUILD_TYPE Release)
endif()

message("Build type ${CMAKE_BUILD_TYPE}")

#--------------------------------------------------------------------------

execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

#--------------------------------------------------------------------------

configure_file(
    "${CMAKE_SOURCE_DIR}/config.h.in"
    "${CMAKE_BINARY_DIR}/config.h"
    @ONLY
)

#--------------------------------------------------------------------------

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak,undefined")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,leak,undefined")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,leak,undefined")
endif()

#--------------------------------------------------------------------------

add_executable(argonOneUpLidMonitor src/argonOneUpLidMonitor.cxx
                                    src/main.cxx)
target_include_directories(argonOneUpLidMonitor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(argonOneUpLidMonitor ${GPIOD_LIBRARIES}
                                           ${SYSTEMD_LIBRARIES})
set_property(TARGET argonOneUpLidMonitor PROPERTY SKIP_BUILD_RPATH TRUE)
install (TARGETS argonOneUpLidMonitor RUNTIME DESTINATION bin)

