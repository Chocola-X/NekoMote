#include <nekomote/asset_io/image.hpp>

#include <png.h>
#include <stb_image_write.h>

#include <cstdio>
#include <vector>

namespace nekomote::asset_io
{
namespace
{
struct PngReader
{
    FILE* file{nullptr};
    png_structp png{nullptr};
    png_infop info{nullptr};
    std::vector<png_bytep> rows;
    char error[256]{};
    ~PngReader()
    {
        if (png)
            png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
        if (file)
            std::fclose(file);
    }
};
void png_error(png_structp png, png_const_charp message)
{
    auto& reader = *static_cast<PngReader*>(png_get_error_ptr(png));
    std::snprintf(reader.error, sizeof(reader.error), "%s", message);
    png_longjmp(png, 1);
}
void png_warning(png_structp, png_const_charp)
{
}
bool decode_png(PngReader& reader, ImageRgba8& image)
{
    // All owners live in the caller. A libpng error jumps only across C calls
    // and scalar locals, never across the construction of a C++ RAII object.
    if (setjmp(png_jmpbuf(reader.png)))
        return false;
    png_init_io(reader.png, reader.file);
    png_read_info(reader.png, reader.info);
    const auto width = png_get_image_width(reader.png, reader.info);
    const auto height = png_get_image_height(reader.png, reader.info);
    if (width == 0 || height == 0 || width > 32768 || height > 32768)
        png_error(reader.png, "invalid or unsupported PNG dimensions");
    const auto color = png_get_color_type(reader.png, reader.info);
    const auto depth = png_get_bit_depth(reader.png, reader.info);
    const bool transparency = png_get_valid(reader.png, reader.info, PNG_INFO_tRNS) != 0;
    if (depth == 16)
        png_set_strip_16(reader.png);
    if (color == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(reader.png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(reader.png);
    if (transparency)
        png_set_tRNS_to_alpha(reader.png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(reader.png);
    if ((color & PNG_COLOR_MASK_ALPHA) == 0 && !transparency)
        png_set_add_alpha(reader.png, 255, PNG_FILLER_AFTER);
    // No gamma/ICC or alpha premultiplication: textures retain their encoded
    // RGBA bytes, agreeing with the former decoder and the GPU protocol.
    png_set_interlace_handling(reader.png);
    png_read_update_info(reader.png, reader.info);
    const auto stride = static_cast<std::size_t>(width) * 4;
    if (png_get_bit_depth(reader.png, reader.info) != 8 ||
        png_get_channels(reader.png, reader.info) != 4 ||
        png_get_rowbytes(reader.png, reader.info) != stride)
        png_error(reader.png, "unexpected decoded PNG layout");
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels.resize(stride * static_cast<std::size_t>(height));
    reader.rows.resize(height);
    for (std::size_t y = 0; y < height; ++y)
        reader.rows[y] = image.pixels.data() + y * stride;
    png_read_image(reader.png, reader.rows.data());
    png_read_end(reader.png, nullptr);
    return true;
}
} // namespace
ImageResult load_png(const std::filesystem::path& path)
{
    ImageResult r;
    PngReader reader;
    reader.file = std::fopen(path.c_str(), "rb");
    if (!reader.file)
    {
        r.error = path.string() + ": cannot open PNG";
        return r;
    }
    reader.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &reader, png_error, png_warning);
    if (reader.png)
        reader.info = png_create_info_struct(reader.png);
    if (!reader.info)
    {
        r.error = path.string() + ": cannot allocate PNG decoder";
        return r;
    }
    if (!decode_png(reader, r.image))
    {
        r.image = {};
        r.error = path.string() + ": " + reader.error;
        return r;
    }
    r.ok = true;
    return r;
}
std::string write_png(const std::filesystem::path& path, const ImageRgba8& image)
{
    if (image.width <= 0 || image.height <= 0 || image.width > 32768 || image.height > 32768 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
        return "invalid RGBA image";
    if (!stbi_write_png(path.string().c_str(), image.width, image.height, 4, image.pixels.data(),
                        image.width * 4))
        return "cannot write PNG: " + path.string();
    return {};
}
} // namespace nekomote::asset_io
