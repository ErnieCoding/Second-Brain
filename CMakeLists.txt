cmake_minimum_required(VERSION 3.16)
project(second-brain CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Target Windows 10+ to expose at_quick_exit, quick_exit, timespec_get in UCRT
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

# ── Source files ──────────────────────────────────────────────────────────────
set(COMMON_SOURCES
    src/main.cpp
    src/logger.cpp
    src/platform.cpp
    src/config.cpp
    src/git_ops.cpp
    src/sync_loop.cpp
    src/conflict_ui.cpp
)

if(WIN32)
    list(APPEND COMMON_SOURCES src/service_win32.cpp)
else()
    list(APPEND COMMON_SOURCES src/service_linux.cpp)
endif()

add_executable(second-brain ${COMMON_SOURCES})

# ── Headers ───────────────────────────────────────────────────────────────────
target_include_directories(second-brain PRIVATE
    include
    third_party       # for #include <nlohmann/json.hpp>
)

# ── libgit2 ───────────────────────────────────────────────────────────────────
if(WIN32)
    # Try ucrt64 first (preferred, matches the default MSYS2 GCC 15 toolchain),
    # then fall back to mingw64.
    find_library(LIBGIT2 git2
        PATHS
            "C:/msys64/ucrt64/lib"
            "C:/msys64/mingw64/lib"
        NO_DEFAULT_PATH
    )
    if(NOT LIBGIT2)
        find_library(LIBGIT2 libgit2
            PATHS
                "C:/msys64/ucrt64/lib"
                "C:/msys64/mingw64/lib"
            NO_DEFAULT_PATH
        )
    endif()
    if(NOT LIBGIT2)
        message(FATAL_ERROR
            "libgit2 not found.\n"
            "Install it with: pacman -S mingw-w64-ucrt-x86_64-libgit2")
    endif()
    # Pick the matching include dir based on where the library was found
    if(LIBGIT2 MATCHES "ucrt64")
        target_include_directories(second-brain PRIVATE "C:/msys64/ucrt64/include")
    else()
        target_include_directories(second-brain PRIVATE "C:/msys64/mingw64/include")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBGIT2 REQUIRED libgit2)
    target_include_directories(second-brain PRIVATE ${LIBGIT2_INCLUDE_DIRS})
    set(LIBGIT2 ${LIBGIT2_LIBRARIES})
endif()

target_link_libraries(second-brain PRIVATE ${LIBGIT2})

# ── Platform-specific link libraries ─────────────────────────────────────────
if(WIN32)
    target_link_libraries(second-brain PRIVATE advapi32 shell32)
endif()

# ── Copy required DLLs next to the exe on Windows (post-build) ───────────────
# This makes the build/second-brain.exe self-contained without needing
# ucrt64/bin on PATH.
if(WIN32)
    set(MSYS2_UCRT64_BIN "C:/msys64/ucrt64/bin")
    set(MSYS2_MINGW64_BIN "C:/msys64/mingw64/bin")
    # Runtime DLLs needed by the executable
    set(REQUIRED_DLLS
        "${MSYS2_UCRT64_BIN}/libgit2-1.9.dll"
        "${MSYS2_UCRT64_BIN}/libgcc_s_seh-1.dll"
        "${MSYS2_UCRT64_BIN}/libwinpthread-1.dll"
        "${MSYS2_UCRT64_BIN}/libstdc++-6.dll"
        "${MSYS2_UCRT64_BIN}/libssh2-1.dll"
        "${MSYS2_UCRT64_BIN}/libpcre2-8-0.dll"
        "${MSYS2_UCRT64_BIN}/libhttp_parser-2.9.dll"
    )
    foreach(DLL ${REQUIRED_DLLS})
        if(EXISTS "${DLL}")
            add_custom_command(TARGET second-brain POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}" "$<TARGET_FILE_DIR:second-brain>"
                COMMENT "Copying ${DLL}"
            )
        endif()
    endforeach()
endif()

# ── RPATH for Linux installed binary ─────────────────────────────────────────
if(UNIX)
    set_target_properties(second-brain PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
