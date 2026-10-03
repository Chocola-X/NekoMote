#include <nekomote/render_gl/renderer.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nekomote::render_gl
{
namespace
{
constexpr const char* kVertexShader = R"(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
uniform vec2 output_size;
uniform vec3 view;
out vec2 texture_uv;
out vec4 vertex_color;
void main() {
    vec2 pixel=position*view.z+view.xy;
    gl_Position=vec4(pixel.x*2.0/output_size.x-1.0,1.0-pixel.y*2.0/output_size.y,0.0,1.0);
    texture_uv=uv;
    vertex_color=color;
})";
constexpr const char* kFragmentShader = R"(#version 330 core
in vec2 texture_uv;
in vec4 vertex_color;
uniform sampler2D atlas;
out vec4 output_color;
void main() {
    vec4 color=texture(atlas,texture_uv)*vertex_color;
    output_color=vec4(color.rgb*color.a,color.a);
})";
struct Shader
{
    GLuint handle;
    explicit Shader(GLenum type) : handle(glCreateShader(type))
    {
    }
    ~Shader()
    {
        if (handle)
            glDeleteShader(handle);
    }
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    void compile(const char* source)
    {
        glShaderSource(handle, 1, &source, nullptr);
        glCompileShader(handle);
        GLint ok = 0;
        glGetShaderiv(handle, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[2048]{};
            glGetShaderInfoLog(handle, 2048, nullptr, log);
            throw std::runtime_error(log);
        }
    }
};
void check_gl(const char* operation)
{
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": GL error " + std::to_string(error));
}
GLsizeiptr byte_size(std::size_t count, std::size_t element)
{
    if (count > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / element)
        throw std::runtime_error("GPU buffer size exceeds representable range");
    return static_cast<GLsizeiptr>(count * element);
}
} // namespace
struct Renderer::Impl
{
    GLuint program{0}, vao{0}, vbo{0}, ebo{0};
    GLint output_size{-1}, view{-1};
    struct Texture
    {
        TextureId id;
        GLuint handle;
    };
    std::vector<Texture> textures;
    std::vector<DrawCommand> commands;
    std::size_t vertex_capacity{0}, index_capacity{0}, command_capacity{0}, texture_capacity{0};
    ~Impl()
    {
        for (const auto& t : textures)
            glDeleteTextures(1, &t.handle);
        if (ebo)
            glDeleteBuffers(1, &ebo);
        if (vbo)
            glDeleteBuffers(1, &vbo);
        if (vao)
            glDeleteVertexArrays(1, &vao);
        if (program)
            glDeleteProgram(program);
    }
    GLuint texture(TextureId id) const
    {
        const auto it = std::find_if(textures.begin(), textures.end(),
                                     [id](const Texture& t) { return t.id == id; });
        return it == textures.end() ? 0 : it->handle;
    }
};
Renderer::Renderer(GlLoader loader) : impl_(std::make_unique<Impl>())
{
    if (!loader || !gladLoadGL(loader))
        throw std::runtime_error("cannot load OpenGL 3.3");
    if (!GLAD_GL_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 core required");
    Shader vertex(GL_VERTEX_SHADER), fragment(GL_FRAGMENT_SHADER);
    vertex.compile(kVertexShader);
    fragment.compile(kFragmentShader);
    auto& p = *impl_;
    p.program = glCreateProgram();
    glAttachShader(p.program, vertex.handle);
    glAttachShader(p.program, fragment.handle);
    glLinkProgram(p.program);
    GLint ok = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048]{};
        glGetProgramInfoLog(p.program, 2048, nullptr, log);
        throw std::runtime_error(log);
    }
    p.output_size = glGetUniformLocation(p.program, "output_size");
    p.view = glGetUniformLocation(p.program, "view");
    glUseProgram(p.program);
    glUniform1i(glGetUniformLocation(p.program, "atlas"), 0);
    glGenVertexArrays(1, &p.vao);
    glGenBuffers(1, &p.vbo);
    glGenBuffers(1, &p.ebo);
    glBindVertexArray(p.vao);
    glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
                          reinterpret_cast<const void*>(offsetof(RenderVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
                          reinterpret_cast<const void*>(offsetof(RenderVertex, color)));
    check_gl("renderer initialize");
}
Renderer::~Renderer() = default;
void Renderer::reserve(std::size_t vertices, std::size_t indices, std::size_t commands,
                       std::size_t textures)
{
    auto& p = *impl_;
    p.commands.reserve(commands);
    p.textures.reserve(textures);
    glBindVertexArray(p.vao);
    glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
    glBufferData(GL_ARRAY_BUFFER, byte_size(vertices, sizeof(RenderVertex)), nullptr,
                 GL_DYNAMIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, byte_size(indices, sizeof(std::uint32_t)), nullptr,
                 GL_DYNAMIC_DRAW);
    check_gl("reserve buffers");
    p.vertex_capacity = vertices;
    p.index_capacity = indices;
    p.command_capacity = commands;
    p.texture_capacity = textures;
}
void Renderer::upload_texture(TextureId id, const ImageRgba8& image)
{
    auto& p = *impl_;
    if (!id.valid() || p.texture(id) || p.textures.size() >= p.texture_capacity)
        throw std::runtime_error("invalid, duplicate or excessive texture ID");
    GLint max_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (image.width <= 0 || image.height <= 0 || image.width > max_size ||
        image.height > max_size ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
        throw std::runtime_error("invalid or unsupported texture dimensions");
    GLuint handle = 0;
    glGenTextures(1, &handle);
    // Register ownership before any subsequent operation can fail.
    p.textures.push_back({id, handle});
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, handle);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    check_gl("upload texture");
}
void Renderer::prepare(RenderFrame frame)
{
    auto& p = *impl_;
    if (frame.vertices.size() > p.vertex_capacity || frame.indices.size() > p.index_capacity ||
        frame.commands.size() > p.command_capacity)
        throw std::runtime_error("frame exceeds reserved capacity");
    for (auto index : frame.indices)
        if (index >= frame.vertices.size())
            throw std::runtime_error("frame vertex index out of range");
    for (const auto& command : frame.commands)
    {
        if (!p.texture(command.texture) || command.first_index > frame.indices.size() ||
            command.index_count > frame.indices.size() - command.first_index ||
            command.index_count > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
            throw std::runtime_error("invalid draw command");
        if (command.blend != BlendMode::Over)
            throw std::runtime_error("P2 supports over composition only");
    }
    glBindVertexArray(p.vao);
    glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size(frame.vertices.size(), sizeof(RenderVertex)),
                    frame.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    byte_size(frame.indices.size(), sizeof(std::uint32_t)), frame.indices.data());
    p.commands.assign(frame.commands.begin(), frame.commands.end());
    check_gl("prepare frame");
}
void Renderer::render(Target target, const View& view)
{
    if (target.width <= 0 || target.height <= 0)
        return; // minimized output
    const auto& p = *impl_;
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(view.clear[0], view.clear[1], view.clear[2], view.clear[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p.program);
    glBindVertexArray(p.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glUniform2f(p.output_size, static_cast<float>(target.width), static_cast<float>(target.height));
    glUniform3f(p.view, view.origin_x, view.origin_y, view.scale);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (const auto& command : p.commands)
    {
        glBindTexture(GL_TEXTURE_2D, p.texture(command.texture));
        glDrawElements(
            GL_TRIANGLES, static_cast<GLsizei>(command.index_count), GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(command.first_index) *
                                          sizeof(std::uint32_t)));
    }
}
ImageRgba8 Renderer::capture(Target target)
{
    if (target.width <= 0 || target.height <= 0)
        throw std::runtime_error("cannot capture empty target");
    ImageRgba8 image;
    image.width = target.width;
    image.height = target.height;
    image.pixels.resize(static_cast<std::size_t>(target.width) *
                        static_cast<std::size_t>(target.height) * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0, 0, target.width, target.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    check_gl("capture");
    const auto stride = static_cast<std::size_t>(target.width) * 4;
    for (int y = 0; y < target.height / 2; ++y)
        std::swap_ranges(
            image.pixels.begin() +
                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * stride),
            image.pixels.begin() +
                static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) + 1) * stride),
            image.pixels.begin() + static_cast<std::ptrdiff_t>(
                                       static_cast<std::size_t>(target.height - 1 - y) * stride));
    // PNG carries straight alpha. Readback is premultiplied; unassociate only in
    // this diagnostic path so transparent fringes can be compared correctly.
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
    {
        const auto alpha = image.pixels[i + 3];
        if (alpha)
            for (std::size_t c = 0; c < 3; ++c)
                image.pixels[i + c] = static_cast<std::uint8_t>(std::min(
                    255U,
                    (static_cast<unsigned>(image.pixels[i + c]) * 255U + alpha / 2U) / alpha));
    }
    return image;
}
std::string Renderer::diagnostics() const
{
    return std::string(reinterpret_cast<const char*>(glGetString(GL_VENDOR))) + " | " +
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)) + " | " +
           reinterpret_cast<const char*>(glGetString(GL_VERSION));
}
} // namespace nekomote::render_gl
