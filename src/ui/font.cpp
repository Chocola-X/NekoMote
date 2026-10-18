#include <nekomote/asset_io/read_only_file.hpp>
#include <nekomote/ui/font.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma GCC diagnostic pop

#include <fontconfig/fontconfig.h>

namespace nekomote::ui
{

namespace
{

template <typename T, void (*Destroy)(T*)>
using FontconfigPtr = std::unique_ptr<T, decltype(Destroy)>;

enum class FontSearch
{
    Primary,
    Fallback,
};

FontconfigPtr<FcFontSet, FcFontSetDestroy> find_system_fonts(const FcCharSet* required,
                                                             FontSearch search, std::string& error)
{
    FontconfigPtr<FcConfig, FcConfigDestroy> config(FcInitLoadConfigAndFonts(), FcConfigDestroy);
    FontconfigPtr<FcPattern, FcPatternDestroy> pattern(FcPatternCreate(), FcPatternDestroy);
    if (!config || !pattern)
    {
        error = "Fontconfig configuration or pattern initialization failed";
        return {nullptr, FcFontSetDestroy};
    }
    for (const auto* family :
         {"Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans CJK JP", "Source Han Sans SC",
          "Source Han Sans TC", "Source Han Sans JP", "Source Han Sans", "Noto Sans", "DejaVu Sans",
          "Liberation Sans", "Ubuntu", "Cantarell", "sans-serif"})
        FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(family));
    FcPatternAddBool(pattern.get(), FC_OUTLINE, FcTrue);
    FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);
    FontconfigPtr<FcCharSet, FcCharSetDestroy> default_coverage(nullptr, FcCharSetDestroy);
    if (search == FontSearch::Primary)
    {
        default_coverage.reset(FcCharSetCreate());
        if (!default_coverage)
        {
            error = "Fontconfig primary charset allocation failed";
            return {nullptr, FcFontSetDestroy};
        }
        // Prefer a primary covering Latin, simplified/traditional Han and kana.
        // Actual UI/catalog text is checked separately by prepare_text().
        for (const auto cp : {0x0041u, 0x6c49u, 0x6f22u, 0x7e41u, 0x3042u, 0x30abu, 0x8a9eu})
            FcCharSetAddChar(default_coverage.get(), cp);
        required = default_coverage.get();
    }
    if (required)
        FcPatternAddCharSet(pattern.get(), FC_CHARSET, required);
    FcConfigSubstitute(config.get(), pattern.get(), FcMatchPattern);
    FcDefaultSubstitute(pattern.get());
    FcResult result;
    if (search == FontSearch::Primary)
    {
        FontconfigPtr<FcFontSet, FcFontSetDestroy> matches(FcFontSetCreate(), FcFontSetDestroy);
        FontconfigPtr<FcPattern, FcPatternDestroy> match(
            FcFontMatch(config.get(), pattern.get(), &result), FcPatternDestroy);
        if (!matches || (match && !FcFontSetAdd(matches.get(), match.get())))
        {
            error = "Fontconfig primary font matching allocation failed";
            return {nullptr, FcFontSetDestroy};
        }
        if (match)
            match.release(); // FcFontSetAdd transfers ownership to the set.
        return matches;
    }
    FontconfigPtr<FcFontSet, FcFontSetDestroy> matches(
        FcFontSort(config.get(), pattern.get(), FcFalse, nullptr, &result), FcFontSetDestroy);
    if (!matches)
        error = "Fontconfig font matching failed";
    return matches;
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

struct FontMetrics::FontInfo
{
    asset_io::ReadOnlyFile file;
    stbtt_fontinfo value{};
    std::string path;
    int index;
    FontInfo(const std::string& source, int face_index)
        : file(source), path(source), index(face_index)
    {
    }
};

FontMetrics::FontMetrics() = default;
FontMetrics::~FontMetrics() = default;

bool FontMetrics::load_face(const std::string& path, int index)
{
    try
    {
        auto candidate = std::make_unique<FontInfo>(path, index);
        const auto bytes = candidate->file.bytes();
        if (bytes.size() < 12)
        {
            last_error_ = path + ": font file is too short";
            return false;
        }
        const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), index);
        if (offset < 0 || !stbtt_InitFont(&candidate->value, bytes.data(), offset))
        {
            last_error_ =
                path + " (face " + std::to_string(index) + "): unsupported font format or face";
            return false;
        }
        font_faces_.push_back(std::move(candidate));
        return true;
    }
    catch (const std::runtime_error& error)
    {
        last_error_ = path + ": " + error.what();
        return false;
    }
}

bool FontMetrics::init(const std::string& font_path, float pixel_height)
{
    initialized_ = false;
    glyphs_.clear();
    pack_x_ = pack_y_ = pack_row_height_ = 0;
    texture_width_ = texture_height_ = 1024;
    font_faces_.clear();
    last_error_.clear();
    system_fallback_ = font_path.empty();
    if (!system_fallback_)
    {
        if (!load_face(font_path, 0))
            return false;
    }
    else
    {
        std::string first_failure;
        for (const auto search : {FontSearch::Primary, FontSearch::Fallback})
        {
            auto matches = find_system_fonts(nullptr, search, last_error_);
            if (!matches)
                return false;
            for (int i = 0; i < matches->nfont; ++i)
            {
                FcChar8* file = nullptr;
                int index = 0;
                if (FcPatternGetString(matches->fonts[i], FC_FILE, 0, &file) != FcResultMatch)
                    continue;
                FcPatternGetInteger(matches->fonts[i], FC_INDEX, 0, &index);
                // The upper bits encode a variable-font named instance. stb selects
                // the collection face with default axes, not the named instance.
                if (load_face(reinterpret_cast<const char*>(file), index & 0xffff))
                    break;
                if (first_failure.empty())
                    first_failure = last_error_;
            }
            if (!font_faces_.empty())
                break;
        }
        if (font_faces_.empty())
        {
            last_error_ = "Fontconfig found no loadable outline font; install a TrueType/OpenType "
                          "font (CJK coverage is needed for Chinese/Japanese UI)";
            if (!first_failure.empty())
                last_error_ += "; first candidate: " + first_failure;
            return false;
        }
    }
    last_error_.clear();
    auto* info = &font_faces_.front()->value;
    int descent, line_gap;
    stbtt_GetFontVMetrics(info, &ascent_, &descent, &line_gap);
    pixel_height_ = pixel_height;
    scale_ = stbtt_ScaleForPixelHeight(info, pixel_height);
    initialized_ = true;
    return true;
}

bool FontMetrics::prepare_text(const char* text)
{
    if (!initialized_ || !text)
    {
        last_error_ = "font is not initialized or text is null";
        return false;
    }
    last_error_.clear();
    std::vector<unsigned int> codepoints;
    for (auto* p = text; *p;)
        if (const auto cp = decode_utf8(p))
            codepoints.push_back(cp);
    std::sort(codepoints.begin(), codepoints.end());
    codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
    std::vector<unsigned int> missing;
    for (const auto cp : codepoints)
        if (stbtt_FindGlyphIndex(&font_for_codepoint(cp).value, static_cast<int>(cp)) == 0)
            missing.push_back(cp);
    if (system_fallback_ && !missing.empty())
    {
        FontconfigPtr<FcCharSet, FcCharSetDestroy> required(FcCharSetCreate(), FcCharSetDestroy);
        if (!required)
        {
            last_error_ = "Fontconfig charset allocation failed";
            return false;
        }
        for (const auto cp : missing)
            FcCharSetAddChar(required.get(), cp);
        auto matches = find_system_fonts(required.get(), FontSearch::Fallback, last_error_);
        if (!matches)
            return false;
        for (int i = 0; i < matches->nfont && !missing.empty(); ++i)
        {
            FcCharSet* charset = nullptr;
            FcChar8* file = nullptr;
            int index = 0;
            if (FcPatternGetCharSet(matches->fonts[i], FC_CHARSET, 0, &charset) != FcResultMatch ||
                FcCharSetIntersectCount(charset, required.get()) == 0 ||
                FcPatternGetString(matches->fonts[i], FC_FILE, 0, &file) != FcResultMatch)
                continue;
            FcPatternGetInteger(matches->fonts[i], FC_INDEX, 0, &index);
            index &= 0xffff;
            const auto* path = reinterpret_cast<const char*>(file);
            const auto loaded =
                std::any_of(font_faces_.begin(), font_faces_.end(), [path, index](const auto& face)
                            { return face->path == path && face->index == index; });
            if (loaded || !load_face(path, index))
                continue;
            const auto before = missing.size();
            const auto* info = &font_faces_.back()->value;
            std::erase_if(missing,
                          [&](const auto cp)
                          {
                              if (stbtt_FindGlyphIndex(info, static_cast<int>(cp)) == 0)
                                  return false;
                              FcCharSetDelChar(required.get(), cp);
                              // Replace an earlier missing-glyph marker if this text was
                              // drawn before its explicit content preparation.
                              glyphs_.erase(cp);
                              return true;
                          });
            if (before == missing.size())
                font_faces_.pop_back();
        }
    }
    // Missing installed coverage uses the primary face's missing-glyph marker.
    // Rasterization/upload is an explicit content-change operation; frame calls
    // only consume these prepared faces and glyphs.
    last_error_.clear();
    for (const auto cp : codepoints)
        ensure_char(cp);
    return true;
}

const FontMetrics::FontInfo& FontMetrics::font_for_codepoint(unsigned int codepoint) const
{
    if (const auto cached = glyphs_.find(codepoint); cached != glyphs_.end())
        return *cached->second.face;
    for (const auto& face : font_faces_)
        if (stbtt_FindGlyphIndex(&face->value, static_cast<int>(codepoint)) != 0)
            return *face;
    // Preserve the primary face's missing-glyph marker when no installed face
    // provides the character. System font discovery cannot invent missing glyphs.
    return *font_faces_.front();
}

void FontMetrics::render_glyph_bitmap(const Glyph& glyph, std::vector<unsigned char>& bitmap)
{
    const auto* info = &font_for_codepoint(glyph.codepoint).value;
    const float scale = stbtt_ScaleForPixelHeight(info, pixel_height_);
    bitmap.resize(static_cast<std::size_t>(glyph.atlas_w * glyph.atlas_h));
    stbtt_MakeCodepointBitmap(info, bitmap.data(), glyph.atlas_w, glyph.atlas_h, glyph.atlas_w,
                              scale, scale, glyph.codepoint);
}

void FontMetrics::ensure_char(unsigned int codepoint)
{
    if (glyphs_.count(codepoint))
        return;
    const auto& face = font_for_codepoint(codepoint);
    const auto* info = &face.value;
    const float scale = stbtt_ScaleForPixelHeight(info, pixel_height_);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
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
        // Preserve packing position: existing glyphs retain their atlas coordinates.
        // Restarting at zero would overwrite them with the next glyph.
        for (auto& [cp, prepared] : glyphs_)
        {
            std::vector<unsigned char> bitmap;
            render_glyph_bitmap(prepared.value, bitmap);
            if (on_glyph_added)
                on_glyph_added(prepared.value, bitmap.data());
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
    glyph.advance = static_cast<float>(advance_width) * scale;
    std::vector<unsigned char> bitmap;
    render_glyph_bitmap(glyph, bitmap);
    if (on_glyph_added)
        on_glyph_added(glyph, bitmap.data());
    glyphs_[codepoint] = {glyph, &face};
    pack_x_ += w + 2;
    pack_row_height_ = std::max(pack_row_height_, h);
}

float FontMetrics::text_width(const char* text, float size)
{
    if (!initialized_)
        return 0;
    const float eff_size = size * 1.30f;
    float width = 0;
    for (const auto* p = text; *p;)
    {
        const unsigned int cp = decode_utf8(p);
        if (cp == 0)
            continue;
        if (const auto cached = glyphs_.find(cp); cached != glyphs_.end())
        {
            width += cached->second.value.advance * (eff_size / pixel_height_);
            continue;
        }
        const auto* info = &font_for_codepoint(cp).value;
        const float scale = stbtt_ScaleForPixelHeight(info, eff_size);
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
    // Prepare every glyph before generating UVs. An atlas growth midway through
    // a string must not leave its first quads normalized to the old dimensions.
    std::size_t glyph_count = 0;
    for (const auto* p = text; *p;)
    {
        const auto codepoint = decode_utf8(p);
        if (codepoint)
        {
            ensure_char(codepoint);
            ++glyph_count;
        }
    }
    // Reserve once per string, retaining capacity across calls and frames.
    const auto reserve_quads = [glyph_count](std::vector<float>& data)
    {
        const auto required = data.size() + glyph_count * 12;
        if (required > data.capacity())
            data.reserve(std::max(required, data.capacity() * 2));
    };
    reserve_quads(vertices);
    reserve_quads(uvs);
    const float eff_size = size * 1.30f;
    const float size_ratio = eff_size / pixel_height_;
    const float baseline = y + static_cast<float>(ascent_) * scale_ * size_ratio;
    float pen_x = x;
    for (const auto* p = text; *p;)
    {
        const unsigned int cp = decode_utf8(p);
        if (cp == 0)
            continue;
        const auto& g = glyphs_.at(cp).value;
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
