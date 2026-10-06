#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <glad/gl.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma GCC diagnostic pop

#include <fontconfig/fontconfig.h>

namespace nekomote::player::ui
{

namespace
{

std::string find_system_font()
{
    if (!FcInit())
        return {};
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config)
        return {};
    const char* families[] = {"萝莉体 第二版", "FZSJ-KEADKA", "Noto Sans CJK SC",
                              "Noto Sans CJK JP", "WenQuanYi Micro Hei",
                              "Droid Sans Fallback"};
    std::string result;
    for (const auto* family : families)
    {
        FcPattern* pattern =
            FcPatternBuild(nullptr, FC_FAMILY, FcTypeString,
                           reinterpret_cast<const FcChar8*>(family), nullptr);
        if (!pattern)
            continue;
        FcConfigSubstitute(config, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult match_result;
        FcPattern* match = FcFontMatch(config, pattern, &match_result);
        if (match)
        {
            FcChar8* file = nullptr;
            if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
            {
                result = reinterpret_cast<const char*>(file);
                FcPatternDestroy(match);
                FcPatternDestroy(pattern);
                break;
            }
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pattern);
    }
    FcConfigDestroy(config);
    return result;
}

constexpr const char* kRectVertexShader = R"(
#version 330 core
uniform vec2 u_viewport;
in vec2 a_pos;
in vec2 a_uv;
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
    float fill_alpha = 1.0 - smoothstep(-aa, aa, dist);
    if (u_border.a <= 0.0) {
        frag = vec4(u_fill.rgb, u_fill.a * fill_alpha);
        return;
    }
    float border_alpha = 1.0 - smoothstep(-aa, aa, dist + u_border_width);
    float border_only = border_alpha - fill_alpha;
    vec3 rgb = u_fill.rgb * fill_alpha + u_border.rgb * border_only;
    float alpha = u_fill.a * fill_alpha + u_border.a * border_only;
    frag = vec4(rgb, alpha);
}
)";

constexpr const char* kTextVertexShader = R"(
#version 330 core
uniform vec2 u_viewport;
in vec2 a_pos;
in vec2 a_uv;
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
uniform vec2 u_viewport;
in vec2 a_pos;
in vec2 a_uv;
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

}

float ease_out_back(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

float ease_out_cubic(float t)
{
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

FontAtlas::~FontAtlas()
{
    if (texture_)
        glDeleteTextures(1, &texture_);
}

bool FontAtlas::init(const std::string& font_path, float pixel_height)
{
    std::string path = font_path;
    if (path.empty())
        path = find_system_font();
    if (path.empty())
        return false;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const auto size = file.tellg();
    file.seekg(0);
    font_data_.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(font_data_.data()), size);
    if (!file)
        return false;

    font_info_storage_.resize(sizeof(stbtt_fontinfo));
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    if (!stbtt_InitFont(info, font_data_.data(), stbtt_GetFontOffsetForIndex(font_data_.data(), 0)))
        return false;

    pixel_height_ = pixel_height;
    scale_ = stbtt_ScaleForPixelHeight(info, pixel_height);
    initialized_ = true;
    return create_texture();
}

bool FontAtlas::create_texture()
{
    glGenTextures(1, &texture_);
    if (!texture_)
        return false;
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0,
                 GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void FontAtlas::ensure_char(unsigned int codepoint)
{
    if (glyphs_.count(codepoint))
        return;
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info, codepoint, scale_, scale_, &x0, &y0, &x1, &y1);
    const int w = std::max(1, x1 - x0);
    const int h = std::max(1, y1 - y0);
    if (pack_x_ + w > texture_width_)
    {
        pack_x_ = 0;
        pack_y_ += pack_row_height_ + 2;
        pack_row_height_ = 0;
    }
    if (pack_y_ + h > texture_height_)
    {
        texture_width_ *= 2;
        texture_height_ *= 2;
        glDeleteTextures(1, &texture_);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0,
                     GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        pack_x_ = 0;
        pack_y_ = 0;
        pack_row_height_ = 0;
        for (auto& [cp, g] : glyphs_)
        {
            std::vector<unsigned char> bitmap(g.atlas_w * g.atlas_h);
            stbtt_MakeCodepointBitmap(info, bitmap.data(), g.atlas_w, g.atlas_h,
                                      g.atlas_w, scale_, scale_, cp);
            upload_glyph(g, bitmap.data());
        }
    }
    Glyph glyph{};
    glyph.codepoint = codepoint;
    glyph.x0 = x0;
    glyph.y0 = y0;
    glyph.x1 = x1;
    glyph.y1 = y1;
    glyph.atlas_x = pack_x_;
    glyph.atlas_y = pack_y_;
    glyph.atlas_w = w;
    glyph.atlas_h = h;
    int advance_width, left_side_bearing;
    stbtt_GetCodepointHMetrics(info, codepoint, &advance_width, &left_side_bearing);
    glyph.advance = static_cast<float>(advance_width) * scale_;
    std::vector<unsigned char> bitmap(w * h);
    stbtt_MakeCodepointBitmap(info, bitmap.data(), w, h, w, scale_, scale_, codepoint);
    upload_glyph(glyph, bitmap.data());
    glyphs_[codepoint] = glyph;
    pack_x_ += w + 2;
    pack_row_height_ = std::max(pack_row_height_, h);
}

void FontAtlas::upload_glyph(const Glyph& glyph, const unsigned char* bitmap)
{
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, glyph.atlas_x, glyph.atlas_y,
                    glyph.atlas_w, glyph.atlas_h, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float FontAtlas::text_width(const char* text, float size)
{
    if (!initialized_)
        return 0;
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    const float scale = stbtt_ScaleForPixelHeight(info, size);
    float width = 0;
    for (const auto* p = text; *p;)
    {
        unsigned int cp = 0;
        const auto c = static_cast<unsigned char>(*p++);
        if (c < 0x80)
            cp = c;
        else if (c < 0xC0)
            continue;
        else if (c < 0xE0)
        {
            cp = (c & 0x1F) << 6;
            if (*p) cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else if (c < 0xF0)
        {
            cp = (c & 0x0F) << 12;
            if (*p) cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p) cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else
        {
            cp = (c & 0x07) << 18;
            if (*p) cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 12;
            if (*p) cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p) cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, cp, &advance, &lsb);
        width += static_cast<float>(advance) * scale;
    }
    return width;
}

void FontAtlas::draw_text(std::vector<float>& vertices, std::vector<float>& uvs,
                          float x, float y, const char* text, float size)
{
    if (!initialized_)
        return;
    const float size_ratio = size / pixel_height_;
    float pen_x = x;
    for (const auto* p = text; *p;)
    {
        unsigned int cp = 0;
        const auto c = static_cast<unsigned char>(*p++);
        if (c < 0x80)
            cp = c;
        else if (c < 0xC0)
            continue;
        else if (c < 0xE0)
        {
            cp = (c & 0x1F) << 6;
            if (*p) cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else if (c < 0xF0)
        {
            cp = (c & 0x0F) << 12;
            if (*p) cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p) cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else
        {
            cp = (c & 0x07) << 18;
            if (*p) cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 12;
            if (*p) cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p) cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        ensure_char(cp);
        const auto& g = glyphs_[cp];
        const float gx0 = pen_x + static_cast<float>(g.x0) * size_ratio;
        const float gy0 = y + static_cast<float>(g.y0) * size_ratio;
        const float gx1 = pen_x + static_cast<float>(g.x1) * size_ratio;
        const float gy1 = y + static_cast<float>(g.y1) * size_ratio;
        const float tx0 = static_cast<float>(g.atlas_x) / static_cast<float>(texture_width_);
        const float ty0 = static_cast<float>(g.atlas_y) / static_cast<float>(texture_height_);
        const float tx1 = static_cast<float>(g.atlas_x + g.atlas_w) / static_cast<float>(texture_width_);
        const float ty1 = static_cast<float>(g.atlas_y + g.atlas_h) / static_cast<float>(texture_height_);
        vertices.insert(vertices.end(), {gx0, gy0, gx1, gy0, gx1, gy1, gx0, gy0, gx1, gy1, gx0, gy1});
        uvs.insert(uvs.end(), {tx0, ty0, tx1, ty0, tx1, ty1, tx0, ty0, tx1, ty1, tx0, ty1});
        pen_x += g.advance * size_ratio;
    }
}

Renderer::~Renderer()
{
    if (rect_shader_) glDeleteProgram(rect_shader_);
    if (text_shader_) glDeleteProgram(text_shader_);
    if (icon_shader_) glDeleteProgram(icon_shader_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (uv_vbo_) glDeleteBuffers(1, &uv_vbo_);
}

bool Renderer::init(int viewport_width, int viewport_height)
{
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
    if (!create_rect_shader()) return false;
    if (!create_text_shader()) return false;
    if (!create_icon_shader()) return false;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &uv_vbo_);
    initialized_ = true;
    return true;
}

bool Renderer::create_rect_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kRectVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kRectFragmentShader);
    if (!vs || !fs) return false;
    rect_shader_ = link_program(vs, fs);
    return rect_shader_ != 0;
}

bool Renderer::create_text_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTextVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTextFragmentShader);
    if (!vs || !fs) return false;
    text_shader_ = link_program(vs, fs);
    return text_shader_ != 0;
}

bool Renderer::create_icon_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kIconVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kIconFragmentShader);
    if (!vs || !fs) return false;
    icon_shader_ = link_program(vs, fs);
    return icon_shader_ != 0;
}

void Renderer::resize(int viewport_width, int viewport_height)
{
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
}

void Renderer::begin_frame()
{
    rect_vertices_.clear();
    rect_uvs_.clear();
    text_vertices_.clear();
    text_uvs_.clear();
    icon_vertices_.clear();
    icon_uvs_.clear();
    icon_textures_.clear();
}

void Renderer::end_frame()
{
    GLint prev_blend = 0, prev_depth = 0;
    glGetIntegerv(GL_BLEND, &prev_blend);
    glGetIntegerv(GL_DEPTH_TEST, &prev_depth);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    flush_rects();
    flush_text();
    flush_icons();
    if (!prev_blend) glDisable(GL_BLEND);
    if (prev_depth) glEnable(GL_DEPTH_TEST);
}

void Renderer::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                 Color fill, Color border, float border_width)
{
    const float r = std::min(radius, std::min(w, h) * 0.5f);
    rect_vertices_.insert(rect_vertices_.end(), {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    rect_uvs_.insert(rect_uvs_.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
    glUseProgram(rect_shader_);
    glUniform4f(glGetUniformLocation(rect_shader_, "u_fill"), fill.r, fill.g, fill.b, fill.a);
    glUniform4f(glGetUniformLocation(rect_shader_, "u_border"), border.r, border.g, border.b, border.a);
    glUniform1f(glGetUniformLocation(rect_shader_, "u_border_width"), border_width);
    glUniform2f(glGetUniformLocation(rect_shader_, "u_rect_size"), w, h);
    glUniform1f(glGetUniformLocation(rect_shader_, "u_radius"), r);
    glUniform1f(glGetUniformLocation(rect_shader_, "u_pixel_scale"), 1.0f);
    flush_rects();
}

void Renderer::flush_rects()
{
    if (rect_vertices_.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, rect_vertices_.size() * sizeof(float), rect_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, rect_uvs_.size() * sizeof(float), rect_uvs_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glUseProgram(rect_shader_);
    glUniform2f(glGetUniformLocation(rect_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rect_vertices_.size() / 2));
    rect_vertices_.clear();
    rect_uvs_.clear();
}

void Renderer::draw_text(float x, float y, const char* text, float size, Color color)
{
    if (!font_) return;
    font_->draw_text(text_vertices_, text_uvs_, x, y, text, size);
    if (text_vertices_.empty()) return;
    glUseProgram(text_shader_);
    glUniform4f(glGetUniformLocation(text_shader_, "u_color"), color.r, color.g, color.b, color.a);
    flush_text();
}

void Renderer::flush_text()
{
    if (text_vertices_.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, text_vertices_.size() * sizeof(float), text_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, text_uvs_.size() * sizeof(float), text_uvs_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glUseProgram(text_shader_);
    glUniform2f(glGetUniformLocation(text_shader_, "u_viewport"),
                static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_ ? font_->texture_id() : 0);
    glUniform1i(glGetUniformLocation(text_shader_, "u_atlas"), 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(text_vertices_.size() / 2));
    text_vertices_.clear();
    text_uvs_.clear();
}

void Renderer::draw_icon(float x, float y, float w, float h, std::uint32_t texture_id,
                         Color tint)
{
    icon_vertices_.insert(icon_vertices_.end(), {x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h});
    icon_uvs_.insert(icon_uvs_.end(), {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1});
    icon_textures_.push_back(texture_id);
    glUseProgram(icon_shader_);
    glUniform4f(glGetUniformLocation(icon_shader_, "u_tint"), tint.r, tint.g, tint.b, tint.a);
    flush_icons();
}

void Renderer::flush_icons()
{
    if (icon_vertices_.empty()) return;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, icon_vertices_.size() * sizeof(float), icon_vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, uv_vbo_);
    glBufferData(GL_ARRAY_BUFFER, icon_uvs_.size() * sizeof(float), icon_uvs_.data(), GL_STREAM_DRAW);
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

void Renderer::draw_circle(float cx, float cy, float radius, Color fill,
                           Color border, float border_width)
{
    draw_rounded_rect(cx - radius, cy - radius, radius * 2, radius * 2, radius,
                      fill, border, border_width);
}

void Context::init(Renderer* renderer, Style style)
{
    renderer_ = renderer;
    style_ = style;
}

void Context::mouse_move(double x, double y)
{
    mouse_x_ = static_cast<float>(x);
    mouse_y_ = static_cast<float>(y);
}

void Context::mouse_button(int button, int action)
{
    if (button == 0)
    {
        mouse_down_ = action == 1;
        if (action == 1) mouse_pressed_ = true;
        if (action == 0) mouse_released_ = true;
    }
}

bool Context::mouse_in_rect(float x, float y, float w, float h) const
{
    return mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
}

void Context::begin_frame()
{
}

void Context::end_frame()
{
    mouse_pressed_ = false;
    mouse_released_ = false;
}

bool Context::toggle_icon(IconState& icon, std::uint32_t texture_id)
{
    if (!renderer_) return false;
    icon.hover = mouse_in_rect(icon.x, icon.y, icon.size, icon.size);
    if (icon.hover && mouse_pressed_ && !icon.dragging)
    {
        icon.dragging = true;
        icon.drag_offset_x = mouse_x_ - icon.x;
        icon.drag_offset_y = mouse_y_ - icon.y;
    }
    bool clicked = false;
    if (icon.dragging && mouse_released_)
    {
        icon.dragging = false;
        if (icon.hover)
            clicked = true;
    }
    if (icon.dragging)
    {
        icon.x = mouse_x_ - icon.drag_offset_x;
        icon.y = mouse_y_ - icon.drag_offset_y;
        icon.x = std::clamp(icon.x, 0.0f, static_cast<float>(renderer_->viewport_width()) - icon.size);
        icon.y = std::clamp(icon.y, 0.0f, static_cast<float>(renderer_->viewport_height()) - icon.size);
    }
    const float target_scale = icon.dragging ? 0.96f : (icon.hover ? 1.08f : 1.0f);
    icon.press_scale = lerp(icon.press_scale, target_scale, 0.2f);
    const float draw_size = icon.size * icon.press_scale;
    const float offset = (draw_size - icon.size) * 0.5f;
    renderer_->draw_icon(icon.x - offset, icon.y - offset, draw_size, draw_size,
                         texture_id, {1, 1, 1, icon.hover ? 1.0f : 0.85f});
    return clicked;
}

void Context::sidebar(SidebarState& sidebar, const Style& /*style*/)
{
    if (!renderer_) return;
    const float target = sidebar.open ? 1.0f : 0.0f;
    sidebar.animation = lerp(sidebar.animation, target, 0.15f);
}

bool Context::radio_button(const char* label, bool selected, float x, float y,
                           float w, float h, const Style& style)
{
    if (!renderer_) return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    const Color bg = hover ? style.hover_bg : Color(0, 0, 0, 0);
    renderer_->draw_rounded_rect(x, y, w, h, 8, bg, {0, 0, 0, 0});
    const float dot_radius = 7;
    const float dot_cx = x + style.padding + dot_radius;
    const float dot_cy = y + h * 0.5f;
    renderer_->draw_circle(dot_cx, dot_cy, dot_radius,
                           selected ? style.accent : Color(0, 0, 0, 0),
                           style.accent, 2);
    const Color text_color = hover ? style.text_hover : style.text;
    const float text_x = x + style.padding + dot_radius * 2 + 8;
    const float text_y = y + h * 0.5f - style.font_size * 0.5f + 2;
    renderer_->draw_text(text_x, text_y, label, style.font_size, text_color);
    return hover && mouse_pressed_;
}

bool Context::text_button(const char* label, float x, float y, float w, float h,
                          const Style& style)
{
    if (!renderer_) return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    const Color bg = hover ? style.hover_bg : Color(0, 0, 0, 0);
    renderer_->draw_rounded_rect(x, y, w, h, 8, bg, {0, 0, 0, 0});
    const Color text_color = hover ? style.text_hover : style.text;
    if (renderer_ && renderer_->viewport_width() > 0)
    {
        const float tw = 0;
        renderer_->draw_text(x + (w - tw) * 0.5f, y + h * 0.5f - style.font_size * 0.5f + 2,
                             label, style.font_size, text_color);
    }
    return hover && mouse_pressed_;
}

bool Context::toggle_switch(const char* label, bool& value, float x, float y,
                            float w, float h, const Style& style)
{
    if (!renderer_) return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover && mouse_pressed_)
        value = !value;
    const float sw_w = 40, sw_h = 22;
    const float sw_x = x + w - sw_w - style.padding;
    const float sw_y = y + (h - sw_h) * 0.5f;
    renderer_->draw_rounded_rect(sw_x, sw_y, sw_w, sw_h, sw_h * 0.5f,
                                 value ? style.accent : Color(0.8f, 0.8f, 0.8f, 0.6f),
                                 {0, 0, 0, 0});
    const float knob_r = 9;
    const float knob_x = value ? sw_x + sw_w - knob_r - 2 : sw_x + knob_r + 2;
    const float knob_y = sw_y + sw_h * 0.5f;
    renderer_->draw_circle(knob_x, knob_y, knob_r, {1, 1, 1, 1}, {0, 0, 0, 0});
    const Color text_color = hover ? style.text_hover : style.text;
    renderer_->draw_text(x + style.padding, y + h * 0.5f - style.font_size * 0.5f + 2,
                         label, style.font_size, text_color);
    return hover && mouse_pressed_;
}

void Context::label(const char* text, float x, float y, float size, Color color)
{
    if (renderer_)
        renderer_->draw_text(x, y, text, size, color);
}

void Context::divider_line(float x, float y, float w, const Style& style)
{
    if (renderer_)
        renderer_->draw_rounded_rect(x, y, w, 1, 0, style.divider, {0, 0, 0, 0});
}

}
