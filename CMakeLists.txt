cmake_minimum_required(VERSION 3.10)
project(argonOneUpLidMonitor)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BSD REQUIRED libbsd)
pkg_check_modules(GPIOD REQUIRED libgpiod libgpiodcxx)

if (CMAKE_BUILD_TYPE STREQUAL "")
set(CMAKE_BUILD_TYPE Release)
endif()

message("Build type ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

#--------------------------------------------------------------------------

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak,undefined")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,leak,undefined")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,leak,undefined")
endif()

#--------------------------------------------------------------------------

add_executable(argonOneUpLidMonitor src/argonOneUpLidMonitor.cxx)
target_link_libraries(argonOneUpLidMonitor ${BSD_LIBRARIES}
                                           ${GPIOD_LIBRARIES})
set_property(TARGET argonOneUpLidMonitor PROPERTY SKIP_BUILD_RPATH TRUE)
install (TARGETS argonOneUpLidMonitor RUNTIME DESTINATION bin)

