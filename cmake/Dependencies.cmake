# External dependencies fetched and pinned via FetchContent.
#
# Versions are pinned to specific tags/commits so builds are reproducible and do
# not depend on the user's global environment. First configuration requires
# network access; subsequent builds reuse the fetched sources.
#
# License summary:
#   GLFW           - zlib/libpng license (BSD-like)
#   Catch2         - BSL-1.0
#   nlohmann/json  - MIT
#   stb            - Public Domain (MIT fallback)
#
# None of these leak into public headers of nekomote_model (the core library):
# GLFW is only used by apps/preview; Catch2 only by tests; json/stb only by
# asset_io/importer (introduced in later phases).

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- nlohmann/json (header-only) -------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

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
    glad_add_library(nekomote_gl_loader STATIC REPRODUCIBLE API gl:core=3.3)
endif()

# --- Vulkan (render_vk backend) ---------------------------------------------------
# Uses the system Vulkan SDK (headers + loader). No version constraint: the
# code targets Vulkan 1.0 core for maximum driver compatibility. The SDK
# provides glslangValidator for build-time SPIR-V compilation; the runtime
# only needs libvulkan (the ICD loader).
if(NEKOMOTE_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    find_program(NEKOMOTE_GLSLANG_VALIDATOR glslangValidator REQUIRED)
endif()
