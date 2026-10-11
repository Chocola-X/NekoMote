#include <nekomote/ui/font.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
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

namespace nekomote::ui
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

unsigned int decode_utf8(const char*& p)
{
    const auto c = static_cast<unsigned char>(*p++);
    if (c < 0x80)
        return c;
    if (c < 0xC0)
        return 0;
    if (c < 0xE0)
    {
        unsigned int cp = (c & 0x1F) << 6;
        if (*p)
            cp |= static_cast<unsigned char>(*p++) & 0x3F;
        return cp;
    }
    if (c < 0xF0)
    {
        unsigned int cp = (c & 0x0F) << 12;
        if (*p)
            cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
        if (*p)
            cp |= static_cast<unsigned char>(*p++) & 0x3F;
        return cp;
    }
    unsigned int cp = (c & 0x07) << 18;
    if (*p)
        cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 12;
    if (*p)
        cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
    if (*p)
        cp |= static_cast<unsigned char>(*p++) & 0x3F;
    return cp;
}

} // namespace

bool FontMetrics::init(const std::string& font_path, float pixel_height)
{
    initialized_ = false;
    glyphs_.clear();
    pack_x_ = pack_y_ = pack_row_height_ = 0;
    texture_width_ = texture_height_ = 1024;
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
    return true;
}

void FontMetrics::render_glyph_bitmap(const Glyph& glyph, std::vector<unsigned char>& bitmap)
{
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    bitmap.resize(static_cast<std::size_t>(glyph.atlas_w * glyph.atlas_h));
    stbtt_MakeCodepointBitmap(info, bitmap.data(), glyph.atlas_w, glyph.atlas_h, glyph.atlas_w,
                              scale_, scale_, glyph.codepoint);
}

void FontMetrics::ensure_char(unsigned int codepoint)
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
        if (on_atlas_resized)
            on_atlas_resized(texture_width_, texture_height_);
        pack_x_ = 0;
        pack_y_ = 0;
        pack_row_height_ = 0;
        for (auto& [cp, g] : glyphs_)
        {
            std::vector<unsigned char> bitmap;
            render_glyph_bitmap(g, bitmap);
            if (on_glyph_added)
                on_glyph_added(g, bitmap.data());
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
    std::vector<unsigned char> bitmap;
    render_glyph_bitmap(glyph, bitmap);
    if (on_glyph_added)
        on_glyph_added(glyph, bitmap.data());
    glyphs_[codepoint] = glyph;
    pack_x_ += w + 2;
    pack_row_height_ = std::max(pack_row_height_, h);
}

float FontMetrics::text_width(const char* text, float size)
{
    if (!initialized_)
        return 0;
    auto* info = reinterpret_cast<stbtt_fontinfo*>(font_info_storage_.data());
    const float eff_size = size * 1.30f;
    const float scale = stbtt_ScaleForPixelHeight(info, eff_size);
    float width = 0;
    for (const auto* p = text; *p;)
    {
        const unsigned int cp = decode_utf8(p);
        if (cp == 0)
            continue;
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, cp, &advance, &lsb);
        width += static_cast<float>(advance) * scale;
    }
    return width;
}

void FontMetrics::draw_text(std::vector<float>& vertices, std::vector<float>& uvs, float x, float y,
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
        const unsigned int cp = decode_utf8(p);
        if (cp == 0)
            continue;
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

} // namespace nekomote::ui
