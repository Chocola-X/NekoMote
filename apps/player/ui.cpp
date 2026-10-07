#include "ui.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma GCC diagnostic pop

#ifdef NEKOMOTE_HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

namespace nekomote::player::ui
{

namespace
{

std::pair<std::string, int> find_system_font()
{
#ifdef NEKOMOTE_HAVE_FONTCONFIG
    if (!FcInit())
        return {"", 0};
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config)
        return {"", 0};
    const char* families[] = {"Noto Sans CJK SC", "WenQuanYi Micro Hei", "Droid Sans Fallback",
                              "Noto Sans CJK JP", "sans-serif"};
    std::string result_file;
    int result_index = 0;
    for (const auto* family : families)
    {
        FcPattern* pattern = FcPatternBuild(nullptr, FC_FAMILY, FcTypeString,
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
                result_file = reinterpret_cast<const char*>(file);
                FcPatternGetInteger(match, FC_INDEX, 0, &result_index);
                FcPatternDestroy(match);
                FcPatternDestroy(pattern);
                break;
            }
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pattern);
    }
    FcConfigDestroy(config);
    return {result_file, result_index};
#else
    const char* candidates[] = {
        "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    };
    for (const auto* path : candidates)
    {
        if (FILE* f = fopen(path, "rb"))
        {
            fclose(f);
            return {path, 0};
        }
    }
    return {"", 0};
#endif
}

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
    int font_index = 0;
    if (path.empty())
    {
        const auto sys = find_system_font();
        path = sys.first;
        font_index = sys.second;
    }
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
    const int offset = stbtt_GetFontOffsetForIndex(font_data_.data(), font_index);
    if (offset < 0 || !stbtt_InitFont(info, font_data_.data(), offset))
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture_width_, texture_height_, 0, GL_RED,
                     GL_UNSIGNED_BYTE, nullptr);
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
            std::vector<unsigned char> bitmap(static_cast<std::size_t>(g.atlas_w * g.atlas_h));
            stbtt_MakeCodepointBitmap(info, bitmap.data(), g.atlas_w, g.atlas_h, g.atlas_w, scale_,
                                      scale_, cp);
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
    std::vector<unsigned char> bitmap(static_cast<std::size_t>(w * h));
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
    glTexSubImage2D(GL_TEXTURE_2D, 0, glyph.atlas_x, glyph.atlas_y, glyph.atlas_w, glyph.atlas_h,
                    GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float FontAtlas::text_width(const char* text, float size)
{
    if (!initialized_)
        return 0;
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    const float eff_size = size * 1.30f;
    const float scale = stbtt_ScaleForPixelHeight(info, eff_size);
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
            if (*p)
                cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else if (c < 0xF0)
        {
            cp = (c & 0x0F) << 12;
            if (*p)
                cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p)
                cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else
        {
            cp = (c & 0x07) << 18;
            if (*p)
                cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 12;
            if (*p)
                cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p)
                cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, cp, &advance, &lsb);
        width += static_cast<float>(advance) * scale;
    }
    return width;
}

void FontAtlas::draw_text(std::vector<float>& vertices, std::vector<float>& uvs, float x, float y,
                          const char* text, float size)
{
    if (!initialized_)
        return;
    const float eff_size = size * 1.30f;
    const float size_ratio = eff_size / pixel_height_;
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &line_gap);
    const float baseline = y + static_cast<float>(ascent) * scale_ * size_ratio;
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
            if (*p)
                cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else if (c < 0xF0)
        {
            cp = (c & 0x0F) << 12;
            if (*p)
                cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p)
                cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        else
        {
            cp = (c & 0x07) << 18;
            if (*p)
                cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 12;
            if (*p)
                cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
            if (*p)
                cp |= static_cast<unsigned char>(*p++) & 0x3F;
        }
        ensure_char(cp);
        const auto& g = glyphs_[cp];
        const float gx0 = pen_x + static_cast<float>(g.x0) * size_ratio;
        const float gy0 = baseline + static_cast<float>(g.y0) * size_ratio;
        const float gx1 = gx0 + static_cast<float>(g.atlas_w) * size_ratio;
        const float gy1 = gy0 + static_cast<float>(g.atlas_h) * size_ratio;
        const float tx0 = static_cast<float>(g.atlas_x) / static_cast<float>(texture_width_);
        const float ty0 = static_cast<float>(g.atlas_y) / static_cast<float>(texture_height_);
        const float tx1 =
            static_cast<float>(g.atlas_x + g.atlas_w) / static_cast<float>(texture_width_);
        const float ty1 =
            static_cast<float>(g.atlas_y + g.atlas_h) / static_cast<float>(texture_height_);
        vertices.insert(vertices.end(),
                        {gx0, gy0, gx1, gy0, gx1, gy1, gx0, gy0, gx1, gy1, gx0, gy1});
        uvs.insert(uvs.end(), {tx0, ty0, tx1, ty0, tx1, ty1, tx0, ty0, tx1, ty1, tx0, ty1});
        pen_x += g.advance * size_ratio;
    }
}

Renderer::~Renderer()
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

bool Renderer::init(int viewport_width, int viewport_height)
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

bool Renderer::create_rect_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kRectVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kRectFragmentShader);
    if (!vs || !fs)
        return false;
    rect_shader_ = link_program(vs, fs);
    return rect_shader_ != 0;
}

bool Renderer::create_text_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTextVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTextFragmentShader);
    if (!vs || !fs)
        return false;
    text_shader_ = link_program(vs, fs);
    return text_shader_ != 0;
}

bool Renderer::create_icon_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kIconVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kIconFragmentShader);
    if (!vs || !fs)
        return false;
    icon_shader_ = link_program(vs, fs);
    return icon_shader_ != 0;
}

bool Renderer::create_tri_shader()
{
    auto vs = compile_shader(GL_VERTEX_SHADER, kTriVertexShader);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, kTriFragmentShader);
    if (!vs || !fs)
        return false;
    tri_shader_ = link_program(vs, fs);
    return tri_shader_ != 0;
}

void Renderer::resize(int viewport_width, int viewport_height)
{
    viewport_width_ = viewport_width;
    viewport_height_ = viewport_height;
}

void Renderer::begin_frame()
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

void Renderer::end_frame()
{
    global_alpha_ = 1.0f;
    if (!initialized_)
        return;
    glDisable(GL_SCISSOR_TEST);
    flush_rects();
    flush_text();
    flush_icons();
}

void Renderer::push_clip_rect(float x, float y, float w, float h)
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

void Renderer::pop_clip_rect()
{
    if (!initialized_)
        return;
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::draw_rounded_rect(float x, float y, float w, float h, float radius, Color fill,
                                 Color border, float border_width)
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

void Renderer::flush_rects()
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

void Renderer::draw_text(float x, float y, const char* text, float size, Color color)
{
    if (!initialized_ || !font_)
        return;
    color.a *= global_alpha_;
    font_->draw_text(text_vertices_, text_uvs_, x, y, text, size);
    if (text_vertices_.empty())
        return;
    glUseProgram(text_shader_);
    glUniform4f(glGetUniformLocation(text_shader_, "u_color"), color.r, color.g, color.b, color.a);
    flush_text();
}

void Renderer::flush_text()
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
    glBindTexture(GL_TEXTURE_2D, font_ ? font_->texture_id() : 0);
    glUniform1i(glGetUniformLocation(text_shader_, "u_atlas"), 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(text_vertices_.size() / 2));
    text_vertices_.clear();
    text_uvs_.clear();
}

void Renderer::draw_icon(float x, float y, float w, float h, std::uint32_t texture_id, Color tint)
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

void Renderer::flush_icons()
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

void Renderer::draw_circle(float cx, float cy, float radius, Color fill, Color border,
                           float border_width)
{
    draw_rounded_rect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, radius, fill, border,
                      border_width);
}

void Renderer::draw_solid_rect(float x, float y, float w, float h, Color color)
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

void Renderer::draw_arrow(float cx, float cy, float size, bool points_up, Color fill)
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
        if (action == 1)
            mouse_pressed_ = true;
        if (action == 0)
            mouse_released_ = true;
    }
}

void Context::mouse_scroll(double amount)
{
    scroll_delta_ += static_cast<float>(amount);
}

bool Context::mouse_in_rect(float x, float y, float w, float h) const
{
    return mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
}

void Context::begin_frame()
{
    if (renderer_)
        renderer_->begin_frame();
}

void Context::end_frame()
{
    mouse_pressed_ = false;
    mouse_released_ = false;
    scroll_delta_ = 0.0f;
}

bool Context::toggle_icon(IconState& icon, std::uint32_t texture_id)
{
    if (!renderer_)
        return false;
    const float vw = static_cast<float>(renderer_->viewport_width());
    const float vh = static_cast<float>(renderer_->viewport_height());
    if (!icon.initialized)
    {
        icon.x = std::max(8.0f, vw - icon.size - 8.0f);
        icon.y = 8.0f;
        icon.initialized = true;
    }
    else if (!icon.dragging)
    {
        icon.x = std::clamp(icon.x, 8.0f, std::max(8.0f, vw - icon.size - 8.0f));
        icon.y = std::clamp(icon.y, 8.0f, std::max(8.0f, vh - icon.size - 8.0f));
    }
    icon.hover = mouse_in_rect(icon.x, icon.y, icon.size, icon.size);
    if (icon.hover && mouse_pressed_ && !icon.dragging)
    {
        icon.dragging = true;
        icon.drag_offset_x = mouse_x_ - icon.x;
        icon.drag_offset_y = mouse_y_ - icon.y;
        icon.start_x = icon.x;
        icon.start_y = icon.y;
        icon.moved = false;
    }
    bool clicked = false;
    if (icon.dragging)
    {
        icon.x = mouse_x_ - icon.drag_offset_x;
        icon.y = mouse_y_ - icon.drag_offset_y;
        icon.x = std::clamp(icon.x, 8.0f, std::max(8.0f, vw - icon.size - 8.0f));
        icon.y = std::clamp(icon.y, 8.0f, std::max(8.0f, vh - icon.size - 8.0f));
        if (std::abs(icon.x - icon.start_x) + std::abs(icon.y - icon.start_y) > 3.0f)
            icon.moved = true;
        if (mouse_released_ || !mouse_down_)
        {
            icon.dragging = false;
            if (mouse_released_ && !icon.moved && icon.hover)
                clicked = true;
        }
    }
    const float target_scale = icon.dragging ? 0.96f : (icon.hover ? 1.08f : 1.0f);
    icon.press_scale = lerp(icon.press_scale, target_scale, 0.25f);
    const float draw_size = icon.size * icon.press_scale;
    const float offset = (draw_size - icon.size) * 0.5f;
    renderer_->draw_icon(icon.x - offset, icon.y - offset, draw_size, draw_size, texture_id,
                         {1, 1, 1, icon.hover ? 1.0f : 0.88f});
    return clicked;
}

void Context::sidebar(SidebarState& sidebar, float delta_time)
{
    if (!renderer_)
        return;

    const float vw = static_cast<float>(renderer_->viewport_width());
    const float vh = static_cast<float>(renderer_->viewport_height());
    const float header_h = 44.0f;
    const float eff_h = std::max(sidebar.height, header_h);

    // If reopening while partially/completely out of viewport, reset to docked initial
    if (sidebar.open && sidebar.animation <= 0.001f && sidebar.is_free)
    {
        if (sidebar.x + sidebar.width < 16.0f || sidebar.x > vw - 16.0f || sidebar.y > vh - 16.0f)
        {
            sidebar.is_free = false;
            sidebar.x = 8.0f;
            sidebar.y = 8.0f;
        }
    }

    // 1. Process drag start, movement, and release
    if (sidebar.animation > 0.001f)
    {
        const bool hover_header = mouse_in_rect(sidebar.x, sidebar.y, sidebar.width, header_h);
        if (hover_header && mouse_pressed_ && !sidebar.dragging)
        {
            sidebar.dragging = true;
            sidebar.drag_offset_x = mouse_x_ - sidebar.x;
            sidebar.drag_offset_y = mouse_y_ - sidebar.y;
        }
        if (sidebar.dragging)
        {
            sidebar.is_free = true;
            sidebar.x = mouse_x_ - sidebar.drag_offset_x;
            sidebar.y = mouse_y_ - sidebar.drag_offset_y;
            sidebar.x = std::clamp(sidebar.x, 8.0f, std::max(8.0f, vw - sidebar.width - 8.0f));
            sidebar.y = std::clamp(sidebar.y, 8.0f, std::max(8.0f, vh - eff_h - 8.0f));
            if (mouse_released_ || !mouse_down_)
                sidebar.dragging = false;
        }
    }
    else
    {
        sidebar.dragging = false;
    }

    // 2. Animation progress [0.0, 1.0] with delta time
    // Opening: ~0.28s, Closing: ~0.24s (no infinite asymptotic lerp tail)
    const float dt = std::clamp(delta_time, 0.0001f, 0.1f);
    if (sidebar.open)
    {
        const float open_speed = 1.0f / 0.28f;
        sidebar.animation = std::min(1.0f, sidebar.animation + dt * open_speed);
    }
    else
    {
        const float close_speed = 1.0f / 0.24f;
        sidebar.animation = std::max(0.0f, sidebar.animation - dt * close_speed);
        if (sidebar.animation <= 0.001f)
            sidebar.animation = 0.0f;
    }

    // 3. Update sidebar position when docked (!is_free)
    if (!sidebar.is_free)
    {
        const float slide_anim = sidebar.open ? ease_out_back(sidebar.animation)
                                              : (sidebar.animation * sidebar.animation);
        sidebar.x = 8.0f - (sidebar.width + 16.0f) * (1.0f - slide_anim);
        sidebar.y = 8.0f;
    }
    else if (!sidebar.dragging)
    {
        sidebar.x = std::clamp(sidebar.x, 8.0f, std::max(8.0f, vw - sidebar.width - 8.0f));
        sidebar.y = std::clamp(sidebar.y, 8.0f, std::max(8.0f, vh - eff_h - 8.0f));
    }
}

void Context::sidebar(SidebarState& sidebar, const Style& /*style*/, float delta_time)
{
    this->sidebar(sidebar, delta_time);
}

void Context::sidebar_header(const SidebarState& sidebar, const char* title, const Style& style)
{
    if (!renderer_)
        return;
    const float header_h = 44.0f;
    renderer_->draw_text(sidebar.x + 16.0f, sidebar.y + 12.0f, title, style.title_size, style.text);
    renderer_->draw_text(sidebar.x + 16.5f, sidebar.y + 12.0f, title, style.title_size, style.text);
    divider_line(sidebar.x, sidebar.y + header_h - 1.0f, sidebar.width, style);
}

bool Context::drawer_header(const char* label, bool is_open, float x, float y, float w, float h,
                            const Style& style)
{
    if (!renderer_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (is_open)
        renderer_->draw_rounded_rect(x, y, w, h, 0, style.open_bg, {0, 0, 0, 0});
    else if (hover)
        renderer_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    const Color text_col = (hover || is_open) ? style.text_hover : style.text;
    const float ty = y + (h - 15.0f) * 0.5f;
    renderer_->draw_text(x + 16.0f, ty, label, 15.0f, text_col);
    const float arrow_x = x + w - 24.0f;
    const float arrow_y = y + h * 0.5f;
    renderer_->draw_arrow(arrow_x, arrow_y, 4.0f, is_open, text_col);
    divider_line(x, y + h - 1.0f, w, style);
    return hover && mouse_pressed_;
}

bool Context::drawer_item(const char* label, bool is_selected, float x, float y, float w, float h,
                          const Style& style)
{
    if (!renderer_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        renderer_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    else if (is_selected)
        renderer_->draw_rounded_rect(x, y, w, h, 0, Color(0.949f, 0.627f, 0.753f, 0.15f),
                                     {0, 0, 0, 0});
    const Color text_col = (hover || is_selected) ? style.text_hover : style.text;
    const float offset_x = hover ? 35.0f : 30.0f;
    const float ty = y + (h - style.font_size) * 0.5f;
    renderer_->draw_text(x + offset_x, ty, label, style.font_size, text_col);
    return hover && mouse_pressed_;
}

bool Context::radio_row(const char* label, const char* badge, bool selected, float x, float y,
                        float w, float h, const Style& style)
{
    if (!renderer_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        renderer_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    const float dot_r = 7.0f;
    const float dot_cx = x + 30.0f + dot_r;
    const float dot_cy = y + h * 0.5f;
    renderer_->draw_circle(dot_cx, dot_cy, dot_r, selected ? style.accent : Color(0, 0, 0, 0),
                           style.accent, 2.0f);
    const Color text_col = (hover || selected) ? style.text_hover : style.text;
    const float text_x = dot_cx + dot_r + 10.0f;
    const float ty = y + (h - style.font_size) * 0.5f;
    renderer_->draw_text(text_x, ty, label, style.font_size, text_col);
    if (badge && badge[0] != '\0')
    {
        const float bw =
            renderer_->font() ? renderer_->font()->text_width(badge, 11.0f) + 14.0f : 36.0f;
        const float bh = 18.0f;
        const float bx = x + w - bw - 16.0f;
        const float by = y + (h - bh) * 0.5f;
        renderer_->draw_rounded_rect(bx, by, bw, bh, 8.0f, style.badge_bg, {0, 0, 0, 0});
        renderer_->draw_text(bx + 7.0f, by + 2.0f, badge, 11.0f, style.text);
    }
    return hover && mouse_pressed_;
}

bool Context::switch_row(const char* label, bool& value, float x, float y, float w, float h,
                         const Style& style)
{
    if (!renderer_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        renderer_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    const Color text_col = hover ? style.text_hover : style.text;
    const float ty = y + (h - style.font_size) * 0.5f;
    renderer_->draw_text(x + 30.0f, ty, label, style.font_size, text_col);
    const float sw_w = 40.0f, sw_h = 22.0f;
    const float sw_x = x + w - sw_w - 16.0f;
    const float sw_y = y + (h - sw_h) * 0.5f;
    const Color off_bg{0.435f, 0.306f, 0.216f, 0.28f};
    renderer_->draw_rounded_rect(sw_x, sw_y, sw_w, sw_h, 11.0f, value ? style.accent : off_bg,
                                 {0, 0, 0, 0});
    const float knob_r = 9.0f;
    const float knob_x = value ? (sw_x + sw_w - knob_r - 2.0f) : (sw_x + knob_r + 2.0f);
    const float knob_y = sw_y + 11.0f;
    renderer_->draw_circle(knob_x, knob_y, knob_r, Color(1, 1, 1, 1),
                           Color(0.3f, 0.2f, 0.2f, 0.20f), 1.0f);
    if (hover && mouse_pressed_)
    {
        value = !value;
        return true;
    }
    return false;
}

bool Context::reset_button(const char* label, float x, float y, float w, float h,
                           const Style& style)
{
    if (!renderer_)
        return false;
    const bool hover = mouse_in_rect(x, y, w, h);
    if (hover)
        renderer_->draw_rounded_rect(x, y, w, h, 0, style.hover_bg, {0, 0, 0, 0});
    divider_line(x, y, w, style);
    const float ty = y + (h - style.font_size) * 0.5f;
    renderer_->draw_text(x + 30.0f, ty, label, style.font_size, style.reset_text);
    renderer_->draw_text(x + 30.5f, ty, label, style.font_size, style.reset_text);
    return hover && mouse_pressed_;
}

void Context::section_header(const char* label, float x, float y, float /*w*/, float h,
                             const Style& style)
{
    if (!renderer_)
        return;
    const float ty = y + (h - style.font_size) * 0.5f;
    renderer_->draw_text(x + 30.0f, ty, label, style.font_size, style.text_hover);
    renderer_->draw_text(x + 30.5f, ty, label, style.font_size, style.text_hover);
}

void Context::divider_line(float x, float y, float w, const Style& style)
{
    if (renderer_)
        renderer_->draw_solid_rect(x, y, w, 1.0f, style.divider);
}

void Context::label(const char* text, float x, float y, float size, Color color)
{
    if (renderer_)
        renderer_->draw_text(x, y, text, size, color);
}

void Context::draw_scrollbar(float x, float y, float w, float h, float content_height,
                             float scroll_y, const Style& style)
{
    if (!renderer_ || content_height <= h)
        return;
    const float bar_w = 4.0f;
    const float bar_x = x + w - bar_w - 3.0f;
    const float bar_h = std::max(24.0f, h * (h / content_height));
    const float max_scroll = content_height - h;
    const float ratio = std::clamp(scroll_y / max_scroll, 0.0f, 1.0f);
    const float bar_y = y + ratio * (h - bar_h);
    renderer_->draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, 2.0f,
                                 Color(style.accent.r, style.accent.g, style.accent.b, 0.85f),
                                 {0, 0, 0, 0});
}

} // namespace nekomote::player::ui
