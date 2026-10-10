#include <nekomote/render_gl/ui_renderer.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace nekomote::render_gl
{

namespace
{

constexpr const char* kRectVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2 u_viewport;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char* kRectFragmentShader = R"(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform vec4 u_fill;
uniform vec4 u_border;
uniform float u_border_width;
uniform vec2 u_rect_size;
uniform float u_radius;
uniform float u_pixel_scale;
void main() {
    vec2 half_size = u_rect_size * 0.5;
    vec2 p = v_uv * u_rect_size - half_size;
    vec2 inner = half_size - vec2(u_radius);
    vec2 q = abs(p) - inner;
    float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - u_radius;
    float aa = 1.5 / u_pixel_scale;
    float outer_alpha = 1.0 - smoothstep(-aa, aa, dist);
    if (u_border.a <= 0.0 || u_border_width <= 0.0) {
        frag = vec4(u_fill.rgb, u_fill.a * outer_alpha);
        return;
    }
    float inner_alpha = 1.0 - smoothstep(-aa, aa, dist + u_border_width);
    float border_alpha = max(0.0, outer_alpha - inner_alpha);
    vec3 rgb = u_fill.rgb * inner_alpha + u_border.rgb * border_alpha;
    float alpha = u_fill.a * inner_alpha + u_border.a * border_alpha;
    frag = vec4(rgb, alpha);
}
)";

constexpr const char* kTextVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2 u_viewport;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char* kTextFragmentShader = R"(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform vec4 u_color;
uniform sampler2D u_atlas;
void main() {
    float a = texture(u_atlas, v_uv).r;
    frag = vec4(u_color.rgb, u_color.a * a);
}
)";

constexpr const char* kIconVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2 u_viewport;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char* kIconFragmentShader = R"(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform vec4 u_tint;
uniform sampler2D u_texture;
void main() {
    vec4 tex = texture(u_texture, v_uv);
    frag = vec4(tex.rgb * u_tint.rgb, tex.a * u_tint.a);
}
)";

constexpr const char* kTriVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
uniform vec2 u_viewport;
void main() {
    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char* kTriFragmentShader = R"(
#version 330 core
out vec4 frag;
uniform vec4 u_color;
void main() {
    frag = u_color;
}
)";

std::uint32_t compile_shader(GLenum type, const char* source)
{
    std::uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "UI shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::uint32_t link_program(std::uint32_t vertex, std::uint32_t fragment)
{
    std::uint32_t program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "UI program link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

} // namespace

FontAtlas::~FontAtlas()
{
    if (texture_)
        glDeleteTextures(1, &texture_);
}

bool FontAtlas::init(int texture_width, int texture_height)
{
    texture_width_ = texture_width;
    texture_height_ = texture_height;
    glGenTextures(1, &texture_);
    if (!texture_)
        return false;
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void FontAtlas::resize_atlas(int new_width, int new_height)
{
    texture_width_ = new_width;
    texture_height_ = new_height;
    if (texture_)
        glDeleteTextures(1, &texture_);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FontAtlas::upload_glyph(const nekomote::ui::Glyph& glyph, const unsigned char* bitmap)
{
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, glyph.atlas_x, glyph.atlas_y, glyph.atlas_w, glyph.atlas_h,
                    GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

UiRenderer::~UiRenderer()
{
    if (rect_shader_)
        glDeleteProgram(rect_shader_);
    if (text_shader_)
        glDeleteProgram(text_shader_);
    if (icon_shader_)
        glDeleteProgram(icon_shader_);
    if (tri_shader_)
        glDeleteProgram(tri_shader_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    if (vbo_)
        glDeleteBuffers(1, &vbo_);
    if (uv_vbo_)
        glDeleteBuffers(1, &uv_vbo_);
}

bool UiRenderer::init(int viewport_width, int viewport_height)
{
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
    if (!create_rect_shader())
        return false;
    if (!create_text_shader())
        return false;
    if (!create_icon_shader())
        return false;
    if (!create_tri_shader())
        return false;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &uv_vbo_);
    initialized_ = true;
    return true;
}

bool UiRenderer::init_font(const std::string& font_path, float pixel_height)
{
    if (!font_metrics_.init(font_path, pixel_height))
        return false;
    if (!font_atlas_.init(font_metrics_.texture_width(), font_metrics_.texture_height()))
        return false;
    font_metrics_.on_glyph_added = [this](const nekomote::ui::Glyph& g, const unsigned char* bitmap) {
        font_atlas_.upload_glyph(g, bitmap);
    };
    font_metrics_.on_atlas_resized = [this](int w, int h) {
        font_atlas_.resize_atlas(w, h);
    };
    font_ready_ = true;
    return true;
}

std::uint32_t UiRenderer::upload_icon(const ImageRgba8& image)
{
    std::uint32_t id = 0;
    glGenTextures(1, &id);
    if (!id)
        return 0;
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void UiRenderer::destroy_icon(std::uint32_t id)
{
    if (id)
        glDeleteTextures(1, &id);
}

bool UiRenderer::create_rect_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kRectVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kRectFragmentShader);
    if (!vs || !fs)
        return false;
    rect_shader_ = link_program(vs, fs);
    return rect_shader_ != 0;
}

bool UiRenderer::create_text_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTextVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTextFragmentShader);
    if (!vs || !fs)
        return false;
    text_shader_ = link_program(vs, fs);
    return text_shader_ != 0;
}

bool UiRenderer::create_icon_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kIconVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kIconFragmentShader);
    if (!vs || !fs)
        return false;
    icon_shader_ = link_program(vs, fs);
    return icon_shader_ != 0;
}

bool UiRenderer::create_tri_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTriVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTriFragmentShader);
    if (!vs || !fs)
        return false;
    tri_shader_ = link_program(vs, fs);
    return tri_shader_ != 0;
}

void UiRenderer::resize(int viewport_width, int viewport_height)
{
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
}

void UiRenderer::begin_frame()
{
    global_alpha_ = 1.0f;
    if (!initialized_)
        return;
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    rect_vertices_.clear();
    rect_uvs_.clear();
    text_vertices_.clear();
    text_uvs_.clear();
    icon_vertices_.clear();
    icon_uvs_.clear();
    icon_textures_.clear();
}

void UiRenderer::end_frame()
{
    global_alpha_ = 1.0f;
    if (!initialized_)
        return;
    glDisable(GL_SCISSOR_TEST);
    flush_rects();
    flush_text();
    flush_icons();
}

void UiRenderer::push_clip_rect(float x, float y, float w, float h)
{
    if (!initialized_)
        return;
    glEnable(GL_SCISSOR_TEST);
    const GLint sx = static_cast<GLint>(x);
    const GLint sy = static_cast<GLint>(static_cast<float>(viewport_height_) - (y + h));
    const GLsizei sw = static_cast<GLsizei>(std::max(0.0f, w));
    const GLsizei sh = static_cast<GLsizei>(std::max(0.0f, h));
    glScissor(sx, sy, sw, sh);
}

void UiRenderer::pop_clip_rect()
{
    if (!initialized_)
        return;
    glDisable(GL_SCISSOR_TEST);
}

void UiRenderer::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                   nekomote::ui::Color fill, nekomote::ui::Color border,
                                   float border_width)
{
    fill.a *= global_alpha_;
    border.a *= global_alpha_;
    if (!initialized_)
        return;
    const float r = std::min(radius, std::min(w, h) * 0.5f);
    rect_vertices_.insert(rect_vertices_.end(),
                          {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    rect_uvs_.insert(rect_uvs_.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
    glUseProgram(rect_shader_);
    glUniform4f(glGetUniformLocation(rect_shader_, "u_fill"), fill.r, fill.g, fill.b, fill.a);
    glUniform4f(glGetUniformLocation(rect_shader_, "u_border"), border.r, border.g, border.b,
                border.a);
    glUniform1f(glGetUniformLocation(rect_shader_, "u_border_width"), border_width);
    glUniform2f(glGetUniformLocation(rect_shader_, "u_rect_size"), w, h);
    glUniform1f(glGetUniformLocation(rect_shader_, "u_radius"), r);
    glUniform1f(glGetUniformLocation(rect_shader_, "u_pixel_scale"), 1.0f);
    flush_rects();
}

void UiRenderer::flush_rects()
{
    if (rect_vertices_.empty())
        return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rect_vertices_.size() * sizeof(float)),
                 rect_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rect_uvs_.size() * sizeof(float)),
                 rect_uvs_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glUseProgram(rect_shader_);
    glUniform2f(glGetUniformLocation(rect_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rect_vertices_.size() / 2));
    rect_vertices_.clear();
    rect_uvs_.clear();
}

void UiRenderer::draw_text(float x, float y, const char* text, float size,
                           nekomote::ui::Color color)
{
    if (!initialized_ || !font_ready_)
        return;
    color.a *= global_alpha_;
    font_metrics_.draw_text(text_vertices_, text_uvs_, x, y, text, size);
    if (text_vertices_.empty())
        return;
    glUseProgram(text_shader_);
    glUniform4f(glGetUniformLocation(text_shader_, "u_color"), color.r, color.g, color.b, color.a);
    flush_text();
}

void UiRenderer::flush_text()
{
    if (!initialized_ || text_vertices_.empty())
        return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(text_vertices_.size() * sizeof(float)),
                 text_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(text_uvs_.size() * sizeof(float)),
                 text_uvs_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glUseProgram(text_shader_);
    glUniform2f(glGetUniformLocation(text_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_atlas_.texture_id());
    glUniform1i(glGetUniformLocation(text_shader_, "u_atlas"), 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(text_vertices_.size() / 2));
    text_vertices_.clear();
    text_uvs_.clear();
}

void UiRenderer::draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                           nekomote::ui::Color tint)
{
    tint.a *= global_alpha_;
    if (!initialized_)
        return;
    icon_vertices_.insert(icon_vertices_.end(),
                          {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    icon_uvs_.insert(icon_uvs_.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
    icon_textures_.push_back(texture_id);
    glUseProgram(icon_shader_);
    glUniform4f(glGetUniformLocation(icon_shader_, "u_tint"), tint.r, tint.g, tint.b, tint.a);
    flush_icons();
}

void UiRenderer::flush_icons()
{
    if (!initialized_ || icon_vertices_.empty())
        return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(icon_vertices_.size() * sizeof(float)),
                 icon_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(icon_uvs_.size() * sizeof(float)),
                 icon_uvs_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glUseProgram(icon_shader_);
    glUniform2f(glGetUniformLocation(icon_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, icon_textures_.front());
    glUniform1i(glGetUniformLocation(icon_shader_, "u_texture"), 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(icon_vertices_.size() / 2));
    icon_vertices_.clear();
    icon_uvs_.clear();
    icon_textures_.clear();
}

void UiRenderer::draw_circle(float cx, float cy, float radius, nekomote::ui::Color fill,
                             nekomote::ui::Color border, float border_width)
{
    draw_rounded_rect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, radius, fill, border,
                      border_width);
}

void UiRenderer::draw_solid_rect(float x, float y, float w, float h, nekomote::ui::Color color)
{
    color.a *= global_alpha_;
    if (!initialized_)
        return;
    const std::vector<float> tri_verts = {x, y, x + w, y,     x + w, y + h,
                                          x, y, x + w, y + h, x,     y + h};
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tri_verts.size() * sizeof(float)),
                 tri_verts.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDisableVertexAttribArray(1);
    glUseProgram(tri_shader_);
    glUniform2f(glGetUniformLocation(tri_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glUniform4f(glGetUniformLocation(tri_shader_, "u_color"), color.r, color.g, color.b, color.a);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void UiRenderer::draw_arrow(float cx, float cy, float size, bool points_up,
                            nekomote::ui::Color fill)
{
    fill.a *= global_alpha_;
    if (!initialized_)
        return;
    std::vector<float> tri_verts;
    if (points_up)
    {
        tri_verts = {cx - size, cy + size * 0.5f, cx + size, cy + size * 0.5f,
                     cx,        cy - size * 0.5f};
    }
    else
    {
        tri_verts = {cx - size, cy - size * 0.5f, cx + size, cy - size * 0.5f,
                     cx,        cy + size * 0.5f};
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tri_verts.size() * sizeof(float)),
                 tri_verts.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDisableVertexAttribArray(1);
    glUseProgram(tri_shader_);
    glUniform2f(glGetUniformLocation(tri_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glUniform4f(glGetUniformLocation(tri_shader_, "u_color"), fill.r, fill.g, fill.b, fill.a);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void UiRenderer::set_global_alpha(float alpha)
{
    global_alpha_ = std::clamp(alpha, 0.0f, 1.0f);
}

float UiRenderer::global_alpha() const
{
    return global_alpha_;
}

int UiRenderer::viewport_width() const
{
    return viewport_width_;
}

int UiRenderer::viewport_height() const
{
    return viewport_height_;
}

nekomote::ui::FontMetrics* UiRenderer::font()
{
    return &font_metrics_;
}

bool UiRenderer::is_initialized() const
{
    return initialized_;
}

} // namespace nekomote::render_gl
