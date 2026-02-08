cmake_minimum_required(VERSION 3.20)
project(dungeoneer VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(DUNGEONEER_BUILD_TESTS "Build dungeoneer tests" ON)
option(DUNGEONEER_BUILD_DEMOS "Build dungeoneer demos" ON)

add_library(dungeoneer
    src/core.c
    src/io.c
    src/map.c
    src/rng.c
    src/generator/api.c
    src/generator/primitives.c
    src/generator/connectivity.c
    src/generator/constraints.c
    src/generator/metadata.c
    src/generator/rooms_corridors.c
    src/generator/organic_cave.c
)
add_library(dungeoneer::dungeoneer ALIAS dungeoneer)

target_compile_features(dungeoneer PUBLIC c_std_11)
target_include_directories(dungeoneer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(dungeoneer PRIVATE /W4 /WX)
else()
    target_compile_options(dungeoneer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS dungeoneer
    EXPORT dungeoneerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
    EXPORT dungeoneerTargets
    FILE dungeoneerTargets.cmake
    NAMESPACE dungeoneer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dungeoneer
)

if(DUNGEONEER_BUILD_TESTS)
    enable_testing()
    add_executable(dungeoneer_tests tests/test_main.c)
    target_link_libraries(dungeoneer_tests PRIVATE dungeoneer)
    add_test(NAME dungeoneer_tests COMMAND dungeoneer_tests)
endif()

if(DUNGEONEER_BUILD_DEMOS)
    add_executable(dungeoneer_ascii_demo demos/ascii_demo.c)
    target_link_libraries(dungeoneer_ascii_demo PRIVATE dungeoneer)
endif()
