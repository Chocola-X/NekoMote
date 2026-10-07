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
uniform int flip_x;
uniform vec2 projection;
out vec2 texture_uv;
out vec4 vertex_color;
void main() {
    vec2 centered=position*view.z+(view.xy-output_size*0.5);
    if (flip_x != 0) centered.x=-centered.x;
    gl_Position=vec4(centered*projection,0.0,1.0);
    texture_uv=uv;
    vertex_color=color;
})";
constexpr const char* kFragmentShader = R"(#version 330 core
in vec2 texture_uv;
in vec4 vertex_color;
uniform sampler2D atlas;
uniform int premultiply;
uniform int protection_pass;
uniform int mask_write;
uniform int use_mask;
uniform sampler2D alpha_mask;
uniform vec2 mask_output_size;
uniform float global_alpha;
out vec4 output_color;
void main() {
    vec4 sampled=texture(atlas,texture_uv);
    if (mask_write != 0) {
        output_color=vec4(0.0,sampled.a,0.0,0.0);
        return;
    }
    vec4 color=sampled*vertex_color;
    color.a*=global_alpha;
    if (use_mask != 0) color.a*=texture(alpha_mask,gl_FragCoord.xy/mask_output_size).g;
    if (protection_pass != 0 && color.a < 0.0000001) discard;
    output_color=vec4(premultiply != 0 ? color.rgb*color.a : color.rgb,color.a);
})";
constexpr const char* kBlitVertex = R"(#version 330 core
out vec2 texture_uv;
void main() {
    vec2 p=vec2((gl_VertexID << 1) & 2,gl_VertexID & 2);
    texture_uv=p;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
})";
constexpr const char* kBlitFragment = R"(#version 330 core
in vec2 texture_uv;
uniform sampler2D scene;
uniform float global_alpha;
out vec4 output_color;
void main() {
    vec4 c=texture(scene,texture_uv);
    c.a*=global_alpha;
    if(c.a<=0.003) discard;
    output_color=vec4(c.rgb*c.a,c.a);
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
GLuint link_program(const char* vertex_source, const char* fragment_source)
{
    Shader vertex(GL_VERTEX_SHADER), fragment(GL_FRAGMENT_SHADER);
    vertex.compile(vertex_source);
    fragment.compile(fragment_source);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex.handle);
    glAttachShader(program, fragment.handle);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048]{};
        glGetProgramInfoLog(program, 2048, nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(log);
    }
    return program;
}
struct Intermediate
{
    GLuint framebuffer{0}, texture{0};
    int width{0}, height{0};
    ~Intermediate()
    {
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
        if (texture)
            glDeleteTextures(1, &texture);
    }
    Intermediate() = default;
    Intermediate(const Intermediate&) = delete;
    Intermediate& operator=(const Intermediate&) = delete;
};
void check_gl(const char* operation)
{
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": GL error " + std::to_string(error));
}
std::unique_ptr<Intermediate> intermediate(int width, int height, GLint format)
{
    auto target = std::make_unique<Intermediate>();
    target->width = width;
    target->height = height;
    glGenTextures(1, &target->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target->texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA,
                 format == GL_RGB5_A1 ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &target->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("incomplete intermediate");
    check_gl("allocate intermediate");
    return target;
}
int floor_power_two(int value) noexcept
{
    int result = 1;
    while (result <= value / 2)
        result *= 2;
    return result;
}
GLsizeiptr byte_size(std::size_t count, std::size_t element)
{
    if (count > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / element)
        throw std::runtime_error("GPU buffer size exceeds representable range");
    return static_cast<GLsizeiptr>(count * element);
}
} // namespace
namespace
{
struct Bounds
{
    float left{std::numeric_limits<float>::infinity()};
    float top{std::numeric_limits<float>::infinity()};
    float right{-std::numeric_limits<float>::infinity()};
    float bottom{-std::numeric_limits<float>::infinity()};
    void include(float x, float y) noexcept
    {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
};
struct Clip
{
    int x{0}, y{0}, width{0}, height{0};
};
Clip clip(const Bounds& bounds, Target target, const View& view) noexcept
{
    if (!std::isfinite(bounds.left))
        return {};
    constexpr float kMaskBorderPixels = 2;
    const auto integer_bound = [](float value, int maximum)
    { return static_cast<int>(std::clamp(value, 0.0f, static_cast<float>(maximum))); };
    const int x = integer_bound(
        std::floor(bounds.left * view.scale + view.origin_x) - kMaskBorderPixels, target.width);
    const int y =
        integer_bound(static_cast<float>(target.height) -
                          std::ceil(bounds.bottom * view.scale + view.origin_y) - kMaskBorderPixels,
                      target.height);
    const int right = integer_bound(
        std::ceil(bounds.right * view.scale + view.origin_x) + kMaskBorderPixels, target.width);
    const int top =
        integer_bound(static_cast<float>(target.height) -
                          std::floor(bounds.top * view.scale + view.origin_y) + kMaskBorderPixels,
                      target.height);
    int clip_x = x, clip_right = right;
    if (view.flip_x)
    {
        clip_x = target.width - right;
        clip_right = target.width - x;
    }
    return {clip_x, y, std::max(0, clip_right - clip_x), std::max(0, top - y)};
}
Clip intersect(Clip a, Clip b) noexcept
{
    const int x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0, std::min(a.x + a.width, b.x + b.width) - x),
            std::max(0, std::min(a.y + a.height, b.y + b.height) - y)};
}
} // namespace
struct Renderer::Impl
{
    GLuint program{0}, blit_program{0}, vao{0}, vbo{0}, ebo{0};
    GLint premultiply{-1}, protection_pass{-1}, mask_write{-1}, use_mask{-1}, mask_output_size{-1};
    GLint global_alpha{-1}, blit_global_alpha{-1};
    ColorComposition composition{ColorComposition::PremultipliedOver};
    std::unique_ptr<Intermediate> color_target, mask_target;
    GLint output_size{-1}, view{-1}, projection{-1}, flip_x{-1};
    struct Texture
    {
        TextureId id;
        GLuint handle;
    };
    std::vector<Texture> textures;
    std::vector<DrawCommand> commands, mask_commands;
    std::vector<MaskDefinition> masks;
    std::vector<Bounds> command_bounds, mask_bounds;
    std::size_t mask_command_capacity{0}, mask_capacity{0};
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
        if (blit_program)
            glDeleteProgram(blit_program);
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
    auto& p = *impl_;
    p.program = link_program(kVertexShader, kFragmentShader);
    p.blit_program = link_program(kBlitVertex, kBlitFragment);
    glUseProgram(p.blit_program);
    glUniform1i(glGetUniformLocation(p.blit_program, "scene"), 0);
    p.blit_global_alpha = glGetUniformLocation(p.blit_program, "global_alpha");
    p.premultiply = glGetUniformLocation(p.program, "premultiply");
    p.protection_pass = glGetUniformLocation(p.program, "protection_pass");
    p.mask_write = glGetUniformLocation(p.program, "mask_write");
    p.use_mask = glGetUniformLocation(p.program, "use_mask");
    p.mask_output_size = glGetUniformLocation(p.program, "mask_output_size");
    p.global_alpha = glGetUniformLocation(p.program, "global_alpha");
    p.output_size = glGetUniformLocation(p.program, "output_size");
    p.view = glGetUniformLocation(p.program, "view");
    p.projection = glGetUniformLocation(p.program, "projection");
    p.flip_x = glGetUniformLocation(p.program, "flip_x");
    glUseProgram(p.program);
    glUniform1i(glGetUniformLocation(p.program, "atlas"), 0);
    glUniform1i(glGetUniformLocation(p.program, "alpha_mask"), 1);
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
void Renderer::reserve(Capacity capacity)
{
    const auto [vertices, indices, commands, textures, mask_commands, masks] = capacity;
    auto& p = *impl_;
    p.command_bounds.reserve(commands);
    p.mask_bounds.reserve(masks);
    p.mask_commands.reserve(mask_commands);
    p.masks.reserve(masks);
    p.mask_command_capacity = mask_commands;
    p.mask_capacity = masks;
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
    if (frame.composition != ColorComposition::PremultipliedOver &&
        frame.composition != ColorComposition::ProtectedStraight)
        throw std::runtime_error("unsupported color composition");
    if (frame.vertices.size() > p.vertex_capacity || frame.indices.size() > p.index_capacity ||
        frame.commands.size() > p.command_capacity ||
        frame.mask_commands.size() > p.mask_command_capacity ||
        frame.masks.size() > p.mask_capacity)
        throw std::runtime_error("frame exceeds reserved capacity");
    for (const auto& vertex : frame.vertices)
    {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.u) ||
            !std::isfinite(vertex.v) ||
            std::any_of(vertex.color.begin(), vertex.color.end(),
                        [](float value) { return !std::isfinite(value); }))
            throw std::runtime_error("nonfinite vertex");
    }
    for (auto index : frame.indices)
        if (index >= frame.vertices.size())
            throw std::runtime_error("frame vertex index out of range");
    const auto validate = [&p, &frame](const DrawCommand& command)
    {
        if (!p.texture(command.texture) || command.first_index > frame.indices.size() ||
            command.index_count > frame.indices.size() - command.first_index ||
            command.index_count > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
            throw std::runtime_error("invalid draw command");
        if (command.protection != ColorProtection::None &&
            (command.protection != ColorProtection::PreserveTranslucent ||
             frame.composition != ColorComposition::ProtectedStraight))
            throw std::runtime_error("color protection requires straight composition");
        if (command.blend != BlendMode::Over &&
            (command.blend != BlendMode::Multiply ||
             frame.composition != ColorComposition::ProtectedStraight ||
             command.protection != ColorProtection::None))
            throw std::runtime_error("unsupported blend mode");
        if (command.mask.valid() && (command.mask.value > frame.masks.size() ||
                                     frame.composition != ColorComposition::ProtectedStraight))
            throw std::runtime_error("invalid mask reference");
    };
    for (const auto& command : frame.commands)
        validate(command);
    for (const auto& command : frame.mask_commands)
    {
        validate(command);
        if (command.mask.valid() || command.protection != ColorProtection::None ||
            command.blend != BlendMode::Over)
            throw std::runtime_error("nested mask commands are unverified");
    }
    for (const auto& mask : frame.masks)
        if (mask.first_command > frame.mask_commands.size() ||
            mask.command_count > frame.mask_commands.size() - mask.first_command)
            throw std::runtime_error("invalid mask command range");
    const auto bounds = [&frame](const DrawCommand& command)
    {
        Bounds result;
        for (std::uint32_t i = 0; i < command.index_count; ++i)
        {
            const auto& vertex = frame.vertices[frame.indices[command.first_index + i]];
            result.include(vertex.x, vertex.y);
        }
        return result;
    };
    p.command_bounds.clear();
    for (const auto& command : frame.commands)
        p.command_bounds.push_back(bounds(command));
    p.mask_bounds.clear();
    for (const auto& mask : frame.masks)
    {
        Bounds result;
        for (std::uint32_t i = 0; i < mask.command_count; ++i)
        {
            const auto bound = bounds(frame.mask_commands[mask.first_command + i]);
            if (std::isfinite(bound.left))
            {
                result.include(bound.left, bound.top);
                result.include(bound.right, bound.bottom);
            }
        }
        p.mask_bounds.push_back(result);
    }
    glBindVertexArray(p.vao);
    glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_size(frame.vertices.size(), sizeof(RenderVertex)),
                    frame.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    byte_size(frame.indices.size(), sizeof(std::uint32_t)), frame.indices.data());
    p.commands.assign(frame.commands.begin(), frame.commands.end());
    p.mask_commands.assign(frame.mask_commands.begin(), frame.mask_commands.end());
    p.masks.assign(frame.masks.begin(), frame.masks.end());
    p.composition = frame.composition;
    check_gl("prepare frame");
}
void Renderer::resize_output(int width, int height)
{
    auto& p = *impl_;
    if (p.composition == ColorComposition::PremultipliedOver || width <= 0 || height <= 0)
        return;
    if (p.color_target && p.color_target->width == width && p.color_target->height == height)
        return;
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum)
        throw std::runtime_error("output exceeds GPU texture dimensions");
    auto color = intermediate(width, height, GL_RGBA8);
    // Reference readback, rather than its upload type, establishes five-bit
    // stored coverage. Alpha is carried in green; the attachment alpha is unused.
    auto mask = intermediate(floor_power_two(width), floor_power_two(height), GL_RGB5_A1);
    GLint green_bits = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &green_bits);
    if (green_bits != 5)
        throw std::runtime_error("five-bit mask coverage attachment unsupported");
    p.color_target = std::move(color);
    p.mask_target = std::move(mask);
}
void Renderer::render(Target target, const View& view, TargetContents contents)
{
    if (contents != TargetContents::Clear && contents != TargetContents::Preserve)
        throw std::runtime_error("invalid target contents policy");
    if (target.width <= 0 || target.height <= 0)
        return; // minimized output
    if (!std::isfinite(view.origin_x) || !std::isfinite(view.origin_y) ||
        !std::isfinite(view.scale) || view.scale <= 0 ||
        std::any_of(view.clear.begin(), view.clear.end(),
                    [](float value) { return !std::isfinite(value); }))
        throw std::runtime_error("invalid output view");
    const auto& p = *impl_;
    const bool straight = p.composition == ColorComposition::ProtectedStraight;
    if (straight && (!p.color_target || p.color_target->width != target.width ||
                     p.color_target->height != target.height))
        throw std::runtime_error("protected composition requires explicit output resize");
    glBindFramebuffer(GL_FRAMEBUFFER, straight ? p.color_target->framebuffer : target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (straight)
        glClearColor(0, 0, 0, 0);
    else
        glClearColor(view.clear[0], view.clear[1], view.clear[2], view.clear[3]);
    if (straight || contents == TargetContents::Clear)
        glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p.program);
    glBindVertexArray(p.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindSampler(1, 0);
    glUniform2f(p.output_size, static_cast<float>(target.width), static_cast<float>(target.height));
    glUniform3f(p.view, view.origin_x, view.origin_y, view.scale);
    glUniform1i(p.flip_x, view.flip_x ? 1 : 0);
    glUniform2f(p.projection, 2.0f / static_cast<float>(target.width),
                -2.0f / static_cast<float>(target.height));
    int premultiply = straight ? 0 : 1;
    glUniform1i(p.premultiply, premultiply);
    glUniform1i(p.protection_pass, 0);
    glUniform1i(p.mask_write, 0);
    glUniform1i(p.use_mask, 0);
    glUniform2f(p.mask_output_size, static_cast<float>(target.width),
                static_cast<float>(target.height));
    glUniform1f(p.global_alpha, straight ? 1.0f : view.alpha);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    std::size_t command_index = 0;
    for (const auto& command : p.commands)
    {
        const auto& bounds = p.command_bounds[command_index++];
        if (command.mask.valid())
        {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0); // avoid read/write feedback on the scratch mask
            glActiveTexture(GL_TEXTURE0);
            glBindFramebuffer(GL_FRAMEBUFFER, p.mask_target->framebuffer);
            glViewport(0, 0, p.mask_target->width, p.mask_target->height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            const auto output_clip =
                intersect(intersect(clip(bounds, target, view),
                                    clip(p.mask_bounds[command.mask.value - 1], target, view)),
                          {0, 0, target.width, target.height});
            const auto scaled = [](int value, int size, int original)
            { return static_cast<int>(static_cast<std::int64_t>(value) * size / original); };
            glEnable(GL_SCISSOR_TEST);
            glScissor(scaled(output_clip.x, p.mask_target->width, target.width),
                      scaled(output_clip.y, p.mask_target->height, target.height),
                      scaled(output_clip.width, p.mask_target->width, target.width),
                      scaled(output_clip.height, p.mask_target->height, target.height));
            glUniform1i(p.mask_write, 1);
            glUniform1i(p.use_mask, 0);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
            const auto& definition = p.masks[command.mask.value - 1];
            for (std::uint32_t i = 0; i < definition.command_count; ++i)
            {
                const auto& draw = p.mask_commands[definition.first_command + i];
                glBindTexture(GL_TEXTURE_2D, p.texture(draw.texture));
                glDrawElements(
                    GL_TRIANGLES, static_cast<GLsizei>(draw.index_count), GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(draw.first_index) *
                                                  sizeof(std::uint32_t)));
            }
            glBindFramebuffer(GL_FRAMEBUFFER, p.color_target->framebuffer);
            glViewport(0, 0, target.width, target.height);
            glScissor(output_clip.x, output_clip.y, output_clip.width, output_clip.height);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, p.mask_target->texture);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(p.mask_write, 0);
            glUniform1i(p.use_mask, 1);
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
            glUniform1i(p.use_mask, 0);
        }
        glBindTexture(GL_TEXTURE_2D, p.texture(command.texture));
        const int command_premultiply = command.blend == BlendMode::Multiply || !straight ? 1 : 0;
        if (command_premultiply != premultiply)
        {
            premultiply = command_premultiply;
            glUniform1i(p.premultiply, premultiply);
        }
        if (straight && command.protection == ColorProtection::PreserveTranslucent)
        {
            glUniform1i(p.protection_pass, 1);
            glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, GL_ZERO, GL_ONE);
            glDrawElements(
                GL_TRIANGLES, static_cast<GLsizei>(command.index_count), GL_UNSIGNED_INT,
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(command.first_index) *
                                              sizeof(std::uint32_t)));
            glUniform1i(p.protection_pass, 0);
        }
        if (command.blend == BlendMode::Multiply)
            // Source V6: opacity-weighted multiply changes RGB only; global alpha
            // remains a final composite operation, as for ordinary straight draws.
            glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        else
            glBlendFuncSeparate(straight ? GL_SRC_ALPHA : GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                GL_ONE_MINUS_SRC_ALPHA);
        glDrawElements(
            GL_TRIANGLES, static_cast<GLsizei>(command.index_count), GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(command.first_index) *
                                          sizeof(std::uint32_t)));
    }
    if (straight)
    {
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        if (contents == TargetContents::Clear)
        {
            glClearColor(view.clear[0], view.clear[1], view.clear[2], view.clear[3]);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glUseProgram(p.blit_program);
        glBindTexture(GL_TEXTURE_2D, p.color_target->texture);
        glUniform1f(p.blit_global_alpha, view.alpha);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, 3);
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
