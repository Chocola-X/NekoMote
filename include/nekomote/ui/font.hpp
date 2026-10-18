#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nekomote::ui
{

struct Glyph
{
    unsigned int codepoint;
    int x0, y0, x1, y1;
    float advance;
    int atlas_x, atlas_y, atlas_w, atlas_h;
};

// CPU-only font metrics. Glyph bitmaps are rendered on demand and delivered
// via on_glyph_added for GPU upload. No GL calls or handles live here.
class FontMetrics
{
  public:
    FontMetrics();
    ~FontMetrics();
    FontMetrics(const FontMetrics&) = delete;
    FontMetrics& operator=(const FontMetrics&) = delete;

    // An empty path uses Fontconfig's system/user configuration and tries each
    // outline font candidate until stb_truetype accepts the primary face.
    bool init(const std::string& font_path, float pixel_height);
    // Call at initialization or when text content changes, with a valid backend
    // upload context if callbacks are attached. Resolves only missing characters
    // and prepares their glyphs. draw_text/text_width never discover/load fonts.
    // All labels to be drawn must pass through this content boundary first.
    bool prepare_text(const char* text);
    const std::string& last_error() const noexcept
    {
        return last_error_;
    }
    void ensure_char(unsigned int codepoint);
    float text_width(const char* text, float size);
    void draw_text(std::vector<float>& vertices, std::vector<float>& uvs, float x, float y,
                   const char* text, float size);
    int texture_width() const
    {
        return texture_width_;
    }
    int texture_height() const
    {
        return texture_height_;
    }

    // Called when a new glyph is prepared (CPU metrics + bitmap ready).
    // The backend uses this to upload the bitmap to a GPU atlas texture.
    std::function<void(const Glyph&, const unsigned char* bitmap)> on_glyph_added;
    // Called when the atlas needs to grow. After this, all existing glyphs
    // are re-delivered via on_glyph_added so the backend can re-upload them.
    std::function<void(int new_w, int new_h)> on_atlas_resized;

  private:
    bool initialized_{false};
    bool system_fallback_{true};
    std::string last_error_;
    struct FontInfo;
    std::vector<std::unique_ptr<FontInfo>> font_faces_;
    bool load_face(const std::string& path, int index);
    const FontInfo& font_for_codepoint(unsigned int codepoint) const;
    int ascent_{0};
    float pixel_height_{16.0f};
    struct PreparedGlyph
    {
        Glyph value;
        const FontInfo* face;
    };
    std::unordered_map<unsigned int, PreparedGlyph> glyphs_;
    int texture_width_{1024};
    int texture_height_{1024};
    int pack_x_{0};
    int pack_y_{0};
    int pack_row_height_{0};
    float scale_{1.0f};

    void render_glyph_bitmap(const Glyph& glyph, std::vector<unsigned char>& bitmap);
};

} // namespace nekomote::ui
