cmake_minimum_required(VERSION 3.16)
project(camlink VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/tcp_receiver.cpp
    src/decoder.cpp
    src/display.cpp
    src/virtual_cam.cpp
    src/adb_helper.cpp
    src/recorder.cpp
    src/osd.cpp
    src/ui_logic.cpp
    src/ui.cpp
)

# Dear ImGui (vendored, header-only library + SDL2/OpenGL3 backends)
set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(camlink ${SOURCES} ${IMGUI_SOURCES})

# Vendored code: silence warnings from -Wall -Wextra / /W4
if(MSVC)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_compile_definitions(camlink PRIVATE
    PROJECT_VERSION_STRING="${PROJECT_VERSION}"
)

# stb_image (header-only, vendored in third_party/)
target_include_directories(camlink PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/imgui
)

# SDL2 resolution order:
#   1. Bundled MinGW devel package in third_party/SDL2 (see setup_deps.ps1)
#   2. System SDL2 via CMake config package / pkg-config / manual lookup
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL2/cmake/sdl2-config.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/SDL2/cmake")
endif()

set(CAMLINK_SDL2_BUNDLED OFF)
find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
    set(CAMLINK_SDL2_BUNDLED ON)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
        if(SDL2_FOUND)
            target_link_libraries(camlink PRIVATE PkgConfig::SDL2)
        endif()
    endif()
endif()

if(NOT SDL2_FOUND AND NOT TARGET PkgConfig::SDL2)
    # Fallback: try to find SDL2 directly
    find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
    find_library(SDL2_LIBRARY SDL2)
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        target_include_directories(camlink PRIVATE ${SDL2_INCLUDE_DIR})
        target_link_libraries(camlink PRIVATE ${SDL2_LIBRARY})
    else()
        message(FATAL_ERROR
            "SDL2 not found. Run pc/setup_deps.ps1 (Windows/MinGW) "
            "or install SDL2 development libraries.")
    endif()
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(camlink PRIVATE SDL2::SDL2)
endif()

# Platform-specific settings
if(WIN32)
    target_link_libraries(camlink PRIVATE ws2_32 opengl32)

    # Embed the app icon in the executable (Explorer / taskbar / Alt-Tab)
    enable_language(RC)
    # windres treats backslashes as escapes; keep forward slashes.
    set(CAMLINK_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/res/camlink.ico")
    configure_file(res/camlink.rc.in "${CMAKE_CURRENT_BINARY_DIR}/camlink.rc" @ONLY)
    target_sources(camlink PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/camlink.rc")
endif()

# Copy SDL2.dll next to the executable for the bundled MinGW package
if(WIN32 AND CAMLINK_SDL2_BUNDLED)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL2_MINGW_ARCH "x86_64-w64-mingw32")
    else()
        set(SDL2_MINGW_ARCH "i686-w64-mingw32")
    endif()
    set(SDL2_DLL "${CMAKE_SOURCE_DIR}/third_party/SDL2/${SDL2_MINGW_ARCH}/bin/SDL2.dll")
    if(EXISTS "${SDL2_DLL}")
        add_custom_command(TARGET camlink POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DLL}" "$<TARGET_FILE_DIR:camlink>")
    endif()
endif()
