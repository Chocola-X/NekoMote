# Read SPIR-V binary files and generate a C++ header with byte arrays.
# Called via: cmake -DSHADER_DIR=... -DSHADER_NAMES="..." -DOUTPUT=... -P generate_spirv.cmake

separate_arguments(SHADER_NAMES UNIX_COMMAND "${SHADER_NAMES}")

set(content "#pragma once\n\n#include <cstddef>\n\nnamespace nekomote::render_vk {\n\n")

foreach(shader ${SHADER_NAMES})
    set(file_path "${SHADER_DIR}/${shader}.spv")
    if(NOT EXISTS "${file_path}")
        message(FATAL_ERROR "SPIR-V file not found: ${file_path}")
    endif()
    file(READ "${file_path}" hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR byte_count "${hex_len} / 2")

    string(APPEND content "inline const unsigned char ${shader}_spv[] = {")
    set(i 0)
    set(first 1)
    while(i LESS hex_len)
        string(SUBSTRING "${hex}" ${i} 2 byte_hex)
        if(first)
            set(first 0)
        else()
            string(APPEND content ", ")
        endif()
        string(APPEND content "0x${byte_hex}")
        math(EXPR i "${i} + 2")
    endwhile()
    string(APPEND content "};\n")
    string(APPEND content "inline constexpr std::size_t ${shader}_size = sizeof(${shader}_spv);\n\n")
endforeach()

string(APPEND content "} // namespace nekomote::render_vk\n")

file(WRITE "${OUTPUT}" "${content}")
message(STATUS "Generated SPIR-V header: ${OUTPUT}")
