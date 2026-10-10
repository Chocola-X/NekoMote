#pragma once

#include <cstdint>
#include <functional>
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
    FontMetrics() = default;
    ~FontMetrics() = default;
    FontMetrics(const FontMetrics&) = delete;
    FontMetrics& operator=(const FontMetrics&) = delete;

    bool init(const std::string& font_path, float pixel_height);
    void ensure_char(unsigned int codepoint);
    float text_width(const char* text, float size);
    void draw_text(std::vector<float>& vertices, std::vector<float>& uvs, float x, float y,
                   const char* text, float size);
    float height() const
    {
        return pixel_height_;
    }
    int texture_width() const
    {
        return texture_width_;
    }
    int texture_height() const
    {
        return texture_height_;
    }
    bool initialized() const
    {
        return initialized_;
    }

    // Called when a new glyph is prepared (CPU metrics + bitmap ready).
    // The backend uses this to upload the bitmap to a GPU atlas texture.
    std::function<void(const Glyph&, const unsigned char* bitmap)> on_glyph_added;
    // Called when the atlas needs to grow. After this, all existing glyphs
    // are re-delivered via on_glyph_added so the backend can re-upload them.
    std::function<void(int new_w, int new_h)> on_atlas_resized;

  private:
    bool initialized_{false};
    std::vector<unsigned char> font_data_;
    std::vector<unsigned char> font_info_storage_;
    float pixel_height_{16.0f};
    std::unordered_map<unsigned int, Glyph> glyphs_;
    int texture_width_{1024};
    int texture_height_{1024};
    int pack_x_{0};
    int pack_y_{0};
    int pack_row_height_{0};
    float scale_{1.0f};

    void render_glyph_bitmap(const Glyph& glyph, std::vector<unsigned char>& bitmap);
};

} // namespace nekomote::ui
