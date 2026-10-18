# Default: pinned source dependencies for development and portable CI archives.
# NEKOMOTE_USE_SYSTEM_DEPS: distribution packages only, with no download fallback.

if(NEKOMOTE_USE_SYSTEM_DEPS)
    include("${CMAKE_CURRENT_LIST_DIR}/SystemDependencies.cmake")
else()
    include(FetchContent)

    set(FETCHCONTENT_QUIET OFF)

    # --- nlohmann/json (header-only) -------------------------------------------------
    set(JSON_SystemInclude ON CACHE BOOL "Treat third-party JSON headers as system includes" FORCE)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(nlohmann_json)

    # GCC 10 lacks floating-point std::from_chars. Use the same strict,
    # locale-independent conversion on every toolchain, only during scene compilation.
    # Header-only, MIT/Apache-2.0/BSL-1.0; no platform/runtime-library dependency.
    FetchContent_Declare(fast_float SYSTEM
        GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
        GIT_TAG v6.1.6
        GIT_SHALLOW TRUE)
    set(FASTFLOAT_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fast_float)

    # --- stb (header-only, single-file image decode) ---------------------------------
    # nothings/stb has no official release tags; pinned to a specific commit.
    # Update deliberately after review. Current pin corresponds to master at P0.
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)
    FetchContent_MakeAvailable(stb)

    # --- Catch2 (test framework) -----------------------------------------------------
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.3
        GIT_SHALLOW    TRUE)
    if(NEKOMOTE_BUILD_TESTS)
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    endif()

    # --- GLFW (preview window + GL context) ------------------------------------------
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    if(NEKOMOTE_BUILD_PREVIEW)
        # Ship both native Linux platforms; GLFW 3.4 selects from XDG_SESSION_TYPE.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
            set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_MakeAvailable(glfw)
    endif()

    # The backend/ABI uses function pointers only; no GLFW or display is needed.
    # The bundled registry makes GL loader generation reproducible.
    if(NEKOMOTE_BUILD_PREVIEW OR NEKOMOTE_BUILD_ABI)
        FetchContent_Declare(glad
            GIT_REPOSITORY https://github.com/Dav1dde/glad.git
            GIT_TAG v2.0.8
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR cmake)
        FetchContent_MakeAvailable(glad)
        # A comma-separated spec keeps glad 2.0.8's CMake output list consistent
        # with MERGE: both APIs are emitted into gl.h/gl.c, not a second gles2.c.
        glad_add_library(nekomote_gl_loader STATIC REPRODUCIBLE MERGE
            API "gl:core=3.3,gles2=3.0" EXTENSIONS GL_EXT_sRGB_write_control)
    endif()
    set(NEKOMOTE_STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
endif()

add_library(nekomote_stb_headers INTERFACE)
target_include_directories(nekomote_stb_headers SYSTEM INTERFACE "${NEKOMOTE_STB_INCLUDE_DIR}")

# --- Vulkan (render_vk backend) ---------------------------------------------------
# Uses the system Vulkan SDK (headers + loader). No version constraint: the
# code targets Vulkan 1.0 core for maximum driver compatibility. The SDK
# provides glslangValidator for build-time SPIR-V compilation; the runtime
# only needs libvulkan (the ICD loader).
if(NEKOMOTE_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    find_program(NEKOMOTE_GLSLANG_VALIDATOR glslangValidator REQUIRED)
endif()
