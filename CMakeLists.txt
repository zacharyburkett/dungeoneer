cmake_minimum_required(VERSION 3.20)
project(dungeoneer VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(DUNGEONEER_BUILD_TESTS "Build dungeoneer tests" ON)
option(DUNGEONEER_BUILD_NUKLEAR_APP "Build optional Nuklear core + GLFW presenter demo" OFF)
option(
    DUNGEONEER_NUKLEAR_AUTO_FETCH
    "Automatically fetch Nuklear and GLFW when building the Nuklear app"
    ON
)

add_library(dungeoneer
    src/core.c
    src/io.c
    src/map.c
    src/rng.c
    src/generator/api.c
    src/generator/defaults.c
    src/generator/request_validation.c
    src/generator/request_snapshot.c
    src/generator/bsp.c
    src/generator/drunkards_walk.c
    src/generator/cellular_automata.c
    src/generator/value_noise.c
    src/generator/rooms_and_mazes.c
    src/generator/room_graph_mst.c
    src/generator/worm_caves.c
    src/generator/simplex_noise.c
    src/generator/process.c
    src/generator/room_types.c
    src/generator/primitives.c
    src/generator/connectivity.c
    src/generator/metadata.c
)
add_library(dungeoneer::dungeoneer ALIAS dungeoneer)

target_compile_features(dungeoneer PUBLIC c_std_11)
target_include_directories(dungeoneer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBPNG QUIET IMPORTED_TARGET libpng)
endif()

if(TARGET PkgConfig::LIBPNG)
    target_link_libraries(dungeoneer PUBLIC PkgConfig::LIBPNG)
else()
    find_package(PNG REQUIRED)
    target_link_libraries(dungeoneer PUBLIC PNG::PNG)
endif()

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

if(DUNGEONEER_BUILD_NUKLEAR_APP)
    set(DUNGEONEER_NUKLEAR_CAN_BUILD OFF)
    set(DUNGEONEER_NUKLEAR_HAS_GLFW_TARGET OFF)
    set(DUNGEONEER_NUKLEAR_FETCHCONTENT_READY OFF)

    if(DUNGEONEER_NUKLEAR_AUTO_FETCH)
        include(FetchContent)
        set(DUNGEONEER_NUKLEAR_FETCHCONTENT_READY ON)
    endif()

    find_path(
        DUNGEONEER_NUKLEAR_INCLUDE_DIR
        nuklear.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
            /opt/homebrew/include
            /usr/local/include
    )
    find_path(
        DUNGEONEER_NUKLEAR_GLFW_INCLUDE_DIR
        nuklear_glfw_gl2.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
            /opt/homebrew/include
            /usr/local/include
    )

    find_package(OpenGL QUIET)
    find_package(glfw3 QUIET)

    if(TARGET glfw::glfw OR TARGET glfw)
        set(DUNGEONEER_NUKLEAR_HAS_GLFW_TARGET ON)
    endif()

    if(
        DUNGEONEER_NUKLEAR_FETCHCONTENT_READY AND
        (
            NOT DUNGEONEER_NUKLEAR_INCLUDE_DIR OR
            NOT DUNGEONEER_NUKLEAR_GLFW_INCLUDE_DIR
        )
    )
        message(STATUS "DUNGEONEER: fetching Nuklear sources")
        FetchContent_Declare(
            dungeoneer_nuklear
            GIT_REPOSITORY https://github.com/vurtun/nuklear.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(dungeoneer_nuklear)
        if(EXISTS "${dungeoneer_nuklear_SOURCE_DIR}/nuklear.h")
            set(DUNGEONEER_NUKLEAR_INCLUDE_DIR "${dungeoneer_nuklear_SOURCE_DIR}")
        endif()
        if(EXISTS "${dungeoneer_nuklear_SOURCE_DIR}/demo/glfw_opengl2/nuklear_glfw_gl2.h")
            set(
                DUNGEONEER_NUKLEAR_GLFW_INCLUDE_DIR
                "${dungeoneer_nuklear_SOURCE_DIR}/demo/glfw_opengl2"
            )
        endif()
    endif()

    if(
        DUNGEONEER_NUKLEAR_FETCHCONTENT_READY AND
        NOT DUNGEONEER_NUKLEAR_HAS_GLFW_TARGET
    )
        message(STATUS "DUNGEONEER: fetching GLFW sources")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            dungeoneer_glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
        )
        FetchContent_MakeAvailable(dungeoneer_glfw)
        if(TARGET glfw::glfw OR TARGET glfw)
            set(DUNGEONEER_NUKLEAR_HAS_GLFW_TARGET ON)
        endif()
    endif()

    if(
        DUNGEONEER_NUKLEAR_INCLUDE_DIR AND
        DUNGEONEER_NUKLEAR_GLFW_INCLUDE_DIR AND
        OPENGL_FOUND AND
        DUNGEONEER_NUKLEAR_HAS_GLFW_TARGET
    )
        set(DUNGEONEER_NUKLEAR_CAN_BUILD ON)
    endif()

    if(DUNGEONEER_NUKLEAR_CAN_BUILD)
        add_library(dungeoneer_nuklear_core apps/nuklear/core.c)
        target_link_libraries(dungeoneer_nuklear_core PUBLIC dungeoneer)
        target_compile_features(dungeoneer_nuklear_core PRIVATE c_std_11)
        target_include_directories(dungeoneer_nuklear_core
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/apps/nuklear
                ${DUNGEONEER_NUKLEAR_INCLUDE_DIR}
                ${DUNGEONEER_NUKLEAR_GLFW_INCLUDE_DIR}
        )

        add_executable(dungeoneer_editor apps/nuklear/glfw_main.c)
        target_compile_features(dungeoneer_editor PRIVATE c_std_11)
        target_link_libraries(
            dungeoneer_editor
            PRIVATE
                dungeoneer_nuklear_core
                OpenGL::GL
        )

        if(TARGET glfw::glfw)
            target_link_libraries(dungeoneer_editor PRIVATE glfw::glfw)
        else()
            target_link_libraries(dungeoneer_editor PRIVATE glfw)
        endif()
        if(APPLE)
            target_compile_definitions(dungeoneer_editor PRIVATE GL_SILENCE_DEPRECATION)
        endif()

        # Backward-compatible target alias while transitioning from demo naming.
        add_executable(dungeoneer_nuklear_demo ALIAS dungeoneer_editor)
    else()
        message(WARNING
            "DUNGEONEER_BUILD_NUKLEAR_APP=ON but required deps were not found. "
            "Need nuklear.h + nuklear_glfw_gl2.h and GLFW/OpenGL (or enable auto-fetch). "
            "Skipping dungeoneer_editor."
        )
    endif()
endif()
