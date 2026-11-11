#include <nekomote/render_gl/ui_renderer.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "shader_source.hpp"

namespace nekomote::render_gl
{

namespace
{

// Most menu batches contain just one color. Keep the attribute constant until
// a different color actually requires a per-vertex buffer.
void append_colors(std::vector<float>& colors, ui::Color& uniform, std::size_t previous_vertices,
                   std::size_t new_vertices, ui::Color color)
{
    const auto append = [&](std::size_t count, ui::Color value)
    {
        for (std::size_t i = 0; i < count; ++i)
            colors.insert(colors.end(), {value.r, value.g, value.b, value.a});
    };
    if (previous_vertices == 0)
        uniform = color;
    else if (colors.empty() && uniform != color)
        append(previous_vertices, uniform);
    if (!colors.empty())
        append(new_vertices, color);
}

constexpr const char* kRectVertexShader = R"(
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
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_viewport;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char* kTextFragmentShader = R"(
in vec2 v_uv;
in vec4 v_color;
out vec4 frag;
uniform sampler2D u_atlas;
void main() {
    float a = texture(u_atlas, v_uv).r;
    frag = vec4(v_color.rgb, v_color.a * a);
}
)";

constexpr const char* kIconVertexShader = R"(
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
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
uniform vec2 u_viewport;
out vec4 v_color;
void main() {
    v_color = a_color;
    vec2 ndc = (a_pos / u_viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

constexpr const char* kTriFragmentShader = R"(
in vec4 v_color;
out vec4 frag;
void main() {
    frag = v_color;
}
)";

std::uint32_t compile_shader(GLenum type, const char* source)
{
    std::uint32_t shader = glCreateShader(type);
    set_shader_source(shader, source);
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
    reset();
}

void FontAtlas::reset()
{
    if (texture_)
        glDeleteTextures(1, &texture_);
    texture_ = 0;
}

bool FontAtlas::init(int texture_width, int texture_height)
{
    reset();
    if (texture_width <= 0 || texture_height <= 0)
        return false;
    texture_width_ = texture_width;
    texture_height_ = texture_height;
    glGenTextures(1, &texture_);
    if (!texture_)
        return false;
    // Linear filtering reads the padding outside each uploaded glyph. Leaving
    // it undefined makes text edges depend on recycled GPU memory.
    const std::vector<unsigned char> zeros(
        static_cast<std::size_t>(texture_width_) * static_cast<std::size_t>(texture_height_), 0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, zeros.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void FontAtlas::resize_atlas(int new_width, int new_height)
{
    if (!init(new_width, new_height))
        throw std::runtime_error("failed to initialize resized font atlas");
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
    reset();
}

void UiRenderer::reset()
{
    font_atlas_.reset();
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
    if (color_vbo_)
        glDeleteBuffers(1, &color_vbo_);
    rect_shader_ = text_shader_ = icon_shader_ = tri_shader_ = 0;
    vao_ = vbo_ = uv_vbo_ = color_vbo_ = 0;
    initialized_ = false;
    font_ready_ = false;
    icon_texture_ = 0;
    clip_enabled_ = false;
    clip_stack_.clear();
    rect_vertices_.clear();
    rect_uvs_.clear();
    text_vertices_.clear();
    text_uvs_.clear();
    text_colors_.clear();
    tri_vertices_.clear();
    tri_colors_.clear();
    icon_vertices_.clear();
    icon_uvs_.clear();
    draw_call_count_ = 0;
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
    glGenBuffers(1, &color_vbo_);
    rect_vertices_.reserve(12);
    rect_uvs_.reserve(12);
    icon_vertices_.reserve(12);
    icon_uvs_.reserve(12);
    text_vertices_.reserve(4096);
    text_uvs_.reserve(4096);
    text_colors_.reserve(8192);
    tri_vertices_.reserve(1024);
    tri_colors_.reserve(2048);
    clip_stack_.reserve(16);
    initialized_ = true;
    return true;
}

bool UiRenderer::init_font(const std::string& font_path, float pixel_height)
{
    // Queued glyphs still refer to the current font and atlas.
    flush_text();
    font_ready_ = false;
    if (!font_metrics_.init(font_path, pixel_height))
        return false;
    if (!font_atlas_.init(font_metrics_.texture_width(), font_metrics_.texture_height()))
        return false;
    font_metrics_.on_glyph_added = [this](const nekomote::ui::Glyph& g, const unsigned char* bitmap)
    { font_atlas_.upload_glyph(g, bitmap); };
    font_metrics_.on_atlas_resized = [this](int w, int h)
    {
        // FontMetrics prepares every glyph before appending the new string.
        // Renormalize queued UVs without clearing vectors borrowed by draw_text.
        const float scale_x =
            static_cast<float>(font_atlas_.texture_width()) / static_cast<float>(w);
        const float scale_y =
            static_cast<float>(font_atlas_.texture_height()) / static_cast<float>(h);
        for (std::size_t i = 0; i < text_uvs_.size(); i += 2)
        {
            text_uvs_[i] *= scale_x;
            text_uvs_[i + 1] *= scale_y;
        }
        font_atlas_.resize_atlas(w, h);
    };
    font_ready_ = true;
    return true;
}

std::uint32_t UiRenderer::upload_icon(ImageViewRgba8 image)
{
    std::uint32_t id = 0;
    glGenTextures(1, &id);
    if (!id)
        return 0;
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 image.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void UiRenderer::destroy_icon(std::uint32_t id)
{
    if (id == icon_texture_)
        flush_icons();
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
    if (!rect_shader_)
        return false;
    rect_uniforms_.viewport = glGetUniformLocation(rect_shader_, "u_viewport");
    rect_uniforms_.fill = glGetUniformLocation(rect_shader_, "u_fill");
    rect_uniforms_.border = glGetUniformLocation(rect_shader_, "u_border");
    rect_uniforms_.border_width = glGetUniformLocation(rect_shader_, "u_border_width");
    rect_uniforms_.rect_size = glGetUniformLocation(rect_shader_, "u_rect_size");
    rect_uniforms_.radius = glGetUniformLocation(rect_shader_, "u_radius");
    rect_uniforms_.pixel_scale = glGetUniformLocation(rect_shader_, "u_pixel_scale");
    return true;
}

bool UiRenderer::create_text_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTextVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTextFragmentShader);
    if (!vs || !fs)
        return false;
    text_shader_ = link_program(vs, fs);
    if (!text_shader_)
        return false;
    text_uniforms_.viewport = glGetUniformLocation(text_shader_, "u_viewport");
    text_uniforms_.sampler = glGetUniformLocation(text_shader_, "u_atlas");
    return true;
}

bool UiRenderer::create_icon_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kIconVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kIconFragmentShader);
    if (!vs || !fs)
        return false;
    icon_shader_ = link_program(vs, fs);
    if (!icon_shader_)
        return false;
    icon_uniforms_.viewport = glGetUniformLocation(icon_shader_, "u_viewport");
    icon_uniforms_.color = glGetUniformLocation(icon_shader_, "u_tint");
    icon_uniforms_.sampler = glGetUniformLocation(icon_shader_, "u_texture");
    return true;
}

bool UiRenderer::create_tri_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTriVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTriFragmentShader);
    if (!vs || !fs)
        return false;
    tri_shader_ = link_program(vs, fs);
    if (!tri_shader_)
        return false;
    tri_uniforms_.viewport = glGetUniformLocation(tri_shader_, "u_viewport");
    return true;
}

void UiRenderer::resize(int viewport_width, int viewport_height)
{
    // Preserve the viewport used by any already queued draws.
    if (viewport_width != viewport_width_ || viewport_height != viewport_height_)
        flush_all();
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
}

void UiRenderer::set_render_target(std::uint32_t framebuffer)
{
    framebuffer_ = framebuffer;
}

void UiRenderer::begin_frame()
{
    global_alpha_ = 1.0f;
    clip_enabled_ = false;
    clip_stack_.clear();
    draw_call_count_ = 0;
    if (!initialized_)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, viewport_width_, viewport_height_);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    rect_vertices_.clear();
    rect_uvs_.clear();
    text_vertices_.clear();
    text_uvs_.clear();
    text_colors_.clear();
    tri_vertices_.clear();
    tri_colors_.clear();
    icon_vertices_.clear();
    icon_uvs_.clear();
    icon_texture_ = 0;
}

void UiRenderer::end_frame()
{
    flush_all();
    global_alpha_ = 1.0f;
    if (!initialized_)
        return;
    glDisable(GL_SCISSOR_TEST);
    clip_enabled_ = false;
    clip_stack_.clear();
}

void UiRenderer::flush_all()
{
    flush_triangles();
    flush_rects();
    flush_text();
    flush_icons();
}

void UiRenderer::push_clip_rect(float x, float y, float w, float h)
{
    if (!initialized_)
        return;
    flush_all();
    GLint sx = static_cast<GLint>(x);
    GLint sy = static_cast<GLint>(static_cast<float>(viewport_height_) - (y + h));
    GLsizei sw = static_cast<GLsizei>(std::max(0.0f, w));
    GLsizei sh = static_cast<GLsizei>(std::max(0.0f, h));
    if (!clip_stack_.empty())
    {
        const auto& prev = clip_stack_.back();
        const GLint x1 = std::max(sx, prev.sx);
        const GLint y1 = std::max(sy, prev.sy);
        const GLint x2 = std::min(sx + sw, prev.sx + prev.sw);
        const GLint y2 = std::min(sy + sh, prev.sy + prev.sh);
        sx = x1;
        sy = y1;
        sw = std::max(0, x2 - x1);
        sh = std::max(0, y2 - y1);
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    const float left = static_cast<float>(sx);
    const float right = left + static_cast<float>(sw);
    const float bottom = static_cast<float>(viewport_height_) - static_cast<float>(sy);
    const float top = bottom - static_cast<float>(sh);
    clip_stack_.push_back({left, top, right, bottom, sx, sy, sw, sh});
    clip_enabled_ = true;
    clip_left_ = left;
    clip_right_ = right;
    clip_top_ = top;
    clip_bottom_ = bottom;
}

void UiRenderer::pop_clip_rect()
{
    flush_all();
    if (!clip_stack_.empty())
        clip_stack_.pop_back();
    if (!clip_stack_.empty())
    {
        const auto& prev = clip_stack_.back();
        glScissor(prev.sx, prev.sy, prev.sw, prev.sh);
        clip_enabled_ = true;
        clip_left_ = prev.left;
        clip_right_ = prev.right;
        clip_top_ = prev.top;
        clip_bottom_ = prev.bottom;
    }
    else
    {
        clip_enabled_ = false;
        if (initialized_)
            glDisable(GL_SCISSOR_TEST);
    }
}

void UiRenderer::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                   nekomote::ui::Color fill, nekomote::ui::Color border,
                                   float border_width)
{
    if (!initialized_)
        return;
    if (clip_enabled_ &&
        (x + w <= clip_left_ || x >= clip_right_ || y + h <= clip_top_ || y >= clip_bottom_))
        return;
    flush_triangles();
    flush_text();
    flush_icons();
    fill.a *= global_alpha_;
    border.a *= global_alpha_;
    const float r = std::min(radius, std::min(w, h) * 0.5f);
    if (!rect_vertices_.empty() && (rect_cur_fill_ != fill || rect_cur_border_ != border ||
                                    rect_cur_border_width_ != border_width || rect_cur_w_ != w ||
                                    rect_cur_h_ != h || rect_cur_radius_ != r))
    {
        flush_rects();
    }
    rect_cur_fill_ = fill;
    rect_cur_border_ = border;
    rect_cur_border_width_ = border_width;
    rect_cur_w_ = w;
    rect_cur_h_ = h;
    rect_cur_radius_ = r;
    rect_vertices_.insert(rect_vertices_.end(),
                          {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    rect_uvs_.insert(rect_uvs_.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
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
    glDisableVertexAttribArray(2);
    glUseProgram(rect_shader_);
    glUniform4f(rect_uniforms_.fill, rect_cur_fill_.r, rect_cur_fill_.g, rect_cur_fill_.b,
                rect_cur_fill_.a);
    glUniform4f(rect_uniforms_.border, rect_cur_border_.r, rect_cur_border_.g, rect_cur_border_.b,
                rect_cur_border_.a);
    glUniform1f(rect_uniforms_.border_width, rect_cur_border_width_);
    glUniform2f(rect_uniforms_.rect_size, rect_cur_w_, rect_cur_h_);
    glUniform1f(rect_uniforms_.radius, rect_cur_radius_);
    glUniform1f(rect_uniforms_.pixel_scale, 1.0f);
    glUniform2f(rect_uniforms_.viewport, static_cast<float>(viewport_width_),
                static_cast<float>(viewport_height_));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rect_vertices_.size() / 2));
    ++draw_call_count_;
    rect_vertices_.clear();
    rect_uvs_.clear();
}

void UiRenderer::draw_text(float x, float y, const char* text, float size,
                           nekomote::ui::Color color)
{
    if (!initialized_ || !font_ready_ || !text)
        return;
    if (clip_enabled_ &&
        (y + size * 2.0f < clip_top_ || y - size > clip_bottom_ || x >= clip_right_))
        return;
    flush_triangles();
    flush_rects();
    flush_icons();
    color.a *= global_alpha_;
    const std::size_t start_vert = text_vertices_.size();
    font_metrics_.draw_text(text_vertices_, text_uvs_, x, y, text, size);
    if (text_vertices_.size() == start_vert)
        return;
    // Origins inside the clip are common and need no bounds scan. For text
    // outside it, inspect generated glyphs rather than assuming a line height.
    if (clip_enabled_ && (x < clip_left_ || x >= clip_right_ || y < clip_top_ || y >= clip_bottom_))
    {
        float left = text_vertices_[start_vert];
        float right = left;
        float top = text_vertices_[start_vert + 1];
        float bottom = top;
        for (std::size_t i = start_vert + 2; i < text_vertices_.size(); i += 2)
        {
            left = std::min(left, text_vertices_[i]);
            right = std::max(right, text_vertices_[i]);
            top = std::min(top, text_vertices_[i + 1]);
            bottom = std::max(bottom, text_vertices_[i + 1]);
        }
        if (right <= clip_left_ || left >= clip_right_ || bottom <= clip_top_ ||
            top >= clip_bottom_)
        {
            text_vertices_.resize(start_vert);
            text_uvs_.resize(start_vert);
            return;
        }
    }
    const std::size_t num_verts = (text_vertices_.size() - start_vert) / 2;
    append_colors(text_colors_, text_color_, start_vert / 2, num_verts, color);
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
    if (text_colors_.empty())
    {
        glDisableVertexAttribArray(2);
        glVertexAttrib4f(2, text_color_.r, text_color_.g, text_color_.b, text_color_.a);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, color_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(text_colors_.size() * sizeof(float)),
                     text_colors_.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glUseProgram(text_shader_);
    glUniform2f(text_uniforms_.viewport, static_cast<float>(viewport_width_),
                static_cast<float>(viewport_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_atlas_.texture_id());
    glUniform1i(text_uniforms_.sampler, 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(text_vertices_.size() / 2));
    ++draw_call_count_;
    text_vertices_.clear();
    text_uvs_.clear();
    text_colors_.clear();
}

void UiRenderer::draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                           nekomote::ui::Color tint)
{
    if (!initialized_)
        return;
    tint.a *= global_alpha_;
    flush_triangles();
    flush_rects();
    flush_text();
    if (icon_texture_ != 0 && (icon_texture_ != texture_id || icon_tint_ != tint))
        flush_icons();
    icon_texture_ = texture_id;
    icon_tint_ = tint;
    icon_vertices_.insert(icon_vertices_.end(),
                          {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    icon_uvs_.insert(icon_uvs_.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
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
    glDisableVertexAttribArray(2);
    glUseProgram(icon_shader_);
    glUniform2f(icon_uniforms_.viewport, static_cast<float>(viewport_width_),
                static_cast<float>(viewport_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, icon_texture_);
    glUniform1i(icon_uniforms_.sampler, 0);
    glUniform4f(icon_uniforms_.color, icon_tint_.r, icon_tint_.g, icon_tint_.b, icon_tint_.a);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(icon_vertices_.size() / 2));
    ++draw_call_count_;
    icon_vertices_.clear();
    icon_uvs_.clear();
    icon_texture_ = 0;
}

void UiRenderer::draw_circle(float cx, float cy, float radius, nekomote::ui::Color fill,
                             nekomote::ui::Color border, float border_width)
{
    draw_rounded_rect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, radius, fill, border,
                      border_width);
}

void UiRenderer::draw_solid_rect(float x, float y, float w, float h, nekomote::ui::Color color)
{
    if (!initialized_)
        return;
    if (clip_enabled_ &&
        (x + w <= clip_left_ || x >= clip_right_ || y + h <= clip_top_ || y >= clip_bottom_))
        return;
    flush_text();
    flush_rects();
    flush_icons();
    color.a *= global_alpha_;
    append_colors(tri_colors_, tri_color_, tri_vertices_.size() / 2, 6, color);
    tri_vertices_.insert(tri_vertices_.end(),
                         {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
}

void UiRenderer::draw_arrow(float cx, float cy, float size, bool points_up,
                            nekomote::ui::Color fill)
{
    if (!initialized_)
        return;
    if (clip_enabled_ && (cx + size <= clip_left_ || cx - size >= clip_right_ ||
                          cy + size <= clip_top_ || cy - size >= clip_bottom_))
        return;
    flush_text();
    flush_rects();
    flush_icons();
    fill.a *= global_alpha_;
    append_colors(tri_colors_, tri_color_, tri_vertices_.size() / 2, 3, fill);
    const float edge = points_up ? size * 0.5f : -size * 0.5f;
    tri_vertices_.insert(tri_vertices_.end(),
                         {cx - size, cy + edge, cx + size, cy + edge, cx, cy - edge});
}

void UiRenderer::flush_triangles()
{
    if (!initialized_ || tri_vertices_.empty())
        return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tri_vertices_.size() * sizeof(float)),
                 tri_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    if (tri_colors_.empty())
    {
        glDisableVertexAttribArray(1);
        glVertexAttrib4f(1, tri_color_.r, tri_color_.g, tri_color_.b, tri_color_.a);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, color_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(tri_colors_.size() * sizeof(float)),
                     tri_colors_.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glDisableVertexAttribArray(2);
    glUseProgram(tri_shader_);
    glUniform2f(tri_uniforms_.viewport, static_cast<float>(viewport_width_),
                static_cast<float>(viewport_height_));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(tri_vertices_.size() / 2));
    ++draw_call_count_;
    tri_vertices_.clear();
    tri_colors_.clear();
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
