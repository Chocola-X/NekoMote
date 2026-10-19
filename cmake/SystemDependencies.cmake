# Distribution-managed dependencies only. Never fetch or silently vendor a missing package.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
# Do not request major 6: FastFloat's config uses SameMajorVersion compatibility,
# while newer distro releases expose the same API used by scene compilation.
find_package(FastFloat CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NEKOMOTE_FLAC REQUIRED IMPORTED_TARGET flac>=1.4)
set(NEKOMOTE_FLAC_TARGET PkgConfig::NEKOMOTE_FLAC)

find_path(NEKOMOTE_STB_INCLUDE_DIR NAMES stb_image_write.h PATH_SUFFIXES stb REQUIRED)
if(NOT EXISTS "${NEKOMOTE_STB_INCLUDE_DIR}/stb_truetype.h")
    message(FATAL_ERROR "System stb must provide stb_image_write.h and stb_truetype.h")
endif()

if(NEKOMOTE_BUILD_TESTS)
    find_package(Catch2 3.5 CONFIG REQUIRED)
endif()
if(NEKOMOTE_BUILD_PREVIEW)
    find_package(glfw3 3.4 CONFIG REQUIRED)
endif()
if(NEKOMOTE_BUILD_PREVIEW OR NEKOMOTE_BUILD_ABI)
    # Distros commonly install the glad 2 CLI without upstream's CMake helpers.
    # Generate the same loader using the installed generator and its registry.
    find_program(NEKOMOTE_GLAD_EXECUTABLE NAMES glad REQUIRED)
    execute_process(COMMAND "${NEKOMOTE_GLAD_EXECUTABLE}" --version
        OUTPUT_VARIABLE glad_version OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE glad_error RESULT_VARIABLE glad_result)
    if(NOT glad_result EQUAL 0 OR NOT glad_version MATCHES "^2[.]")
        message(FATAL_ERROR "System glad 2 is required: ${glad_version} ${glad_error}")
    endif()
    message(STATUS "System glad ${glad_version}: ${NEKOMOTE_GLAD_EXECUTABLE}")
    set(glad_output "${CMAKE_CURRENT_BINARY_DIR}/gladsources/nekomote_gl_loader")
    set(glad_source "${glad_output}/src/gl.c")
    set(glad_headers "${glad_output}/include/glad/gl.h" "${glad_output}/include/KHR/khrplatform.h")
    add_custom_command(OUTPUT "${glad_source}" ${glad_headers}
        COMMAND "${NEKOMOTE_GLAD_EXECUTABLE}" --quiet --reproducible --merge
            --api "gl:core=3.3,gles2=3.0" --extensions GL_EXT_sRGB_write_control
            --out-path "${glad_output}" c
        DEPENDS "${NEKOMOTE_GLAD_EXECUTABLE}"
        COMMENT "Generating GL/GLES loader with system glad ${glad_version}"
        VERBATIM)
    add_library(nekomote_gl_loader STATIC "${glad_source}" ${glad_headers})
    target_include_directories(nekomote_gl_loader SYSTEM PUBLIC "${glad_output}/include")
endif()
