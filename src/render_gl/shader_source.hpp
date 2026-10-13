#pragma once

#include <glad/gl.h>

#include <cstring>

namespace nekomote::render_gl
{
// Called only with loaded functions and a current context, at shader creation.
inline bool is_gles_context()
{
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return version && std::strncmp(version, "OpenGL ES", 9) == 0;
}
inline void set_shader_source(GLuint shader, const char* body)
{
    const char* sources[] = {is_gles_context()
                                 ? "#version 300 es\nprecision highp float;\nprecision highp "
                                   "int;\nprecision highp sampler2D;\n"
                                 : "#version 330 core\n",
                             body};
    glShaderSource(shader, 2, sources, nullptr);
}
} // namespace nekomote::render_gl
