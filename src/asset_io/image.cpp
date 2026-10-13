#include <nekomote/asset_io/image.hpp>

#include <png.h>
#include <stb_image_write.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace nekomote::asset_io
{
namespace
{
// A stateless allocation policy: no mutable cache or device/thread state.
// Each PNG buffer is its own mapping and can be freed on any CPU thread.
class DecodedPixelResource final : public std::pmr::memory_resource
{
    void* do_allocate(std::size_t bytes, std::size_t) override
    {
        void* memory =
            ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED)
            throw std::bad_alloc();
        return memory;
    }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t) override
    {
        ::munmap(memory, bytes);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};
DecodedPixelResource kDecodedPixels;

enum class DecodeMode
{
    Full,
    Bounds,
    Crop
};
struct Bounds
{
    int left{32768}, top{32768}, right{0}, bottom{0};
    void scan(const std::uint8_t* row, int width, int y)
    {
        // Straight-alpha interpolation also uses RGB from alpha-zero pixels.
        // Only the first and last occupied texels of each row affect bounds.
        const auto occupied = [row](int x)
        {
            const auto* p = row + static_cast<std::size_t>(x) * 4;
            return (p[0] | p[1] | p[2] | p[3]) != 0;
        };
        int first = 0;
        while (first < width && !occupied(first))
            ++first;
        if (first == width)
            return;
        int last = width - 1;
        while (last > first && !occupied(last))
            --last;
        left = std::min(left, first);
        top = std::min(top, y);
        right = std::max(right, last + 1);
        bottom = std::max(bottom, y + 1);
    }
};
struct PngReader
{
    FILE* file{nullptr};
    png_structp png{nullptr};
    png_infop info{nullptr};
    std::vector<png_bytep> rows;
    std::vector<std::uint8_t> row;
    Bounds bounds;
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
bool decode_png(PngReader& reader, DecodedImageRgba8& image, DecodeMode mode, TextureRegion region)
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
    const bool interlaced = png_get_interlace_type(reader.png, reader.info) != PNG_INTERLACE_NONE;
    png_set_interlace_handling(reader.png);
    png_read_update_info(reader.png, reader.info);
    const auto stride = static_cast<std::size_t>(width) * 4;
    if (png_get_bit_depth(reader.png, reader.info) != 8 ||
        png_get_channels(reader.png, reader.info) != 4 ||
        png_get_rowbytes(reader.png, reader.info) != stride)
        png_error(reader.png, "unexpected decoded PNG layout");
    if (mode == DecodeMode::Crop && (interlaced || region.source_width != static_cast<int>(width) ||
                                     region.source_height != static_cast<int>(height) ||
                                     !region.valid_for(image.width, image.height)))
        png_error(reader.png, "PNG layout changed during crop decode");
    if (mode == DecodeMode::Full || interlaced)
    {
        // Adam7 needs complete rows across passes; combine into the full image
        // before scanning/cropping. This is a PNG encoding path, not a model
        // compatibility mode. Noninterlaced model atlases never allocate it.
        image.width = static_cast<int>(width);
        image.height = static_cast<int>(height);
        image.pixels.resize(stride * static_cast<std::size_t>(height));
        reader.rows.resize(height);
        for (std::size_t y = 0; y < height; ++y)
            reader.rows[y] = image.pixels.data() + y * stride;
        png_read_image(reader.png, reader.rows.data());
    }
    else
    {
        reader.row.resize(stride);
        if (mode == DecodeMode::Bounds)
        {
            image.width = static_cast<int>(width);
            image.height = static_cast<int>(height);
        }
        else
            image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);
        for (int y = 0; y < static_cast<int>(height); ++y)
        {
            png_read_row(reader.png, reader.row.data(), nullptr);
            if (mode == DecodeMode::Bounds)
                reader.bounds.scan(reader.row.data(), static_cast<int>(width), y);
            else if (y >= region.top && y - region.top < image.height)
                std::memcpy(image.pixels.data() +
                                static_cast<std::size_t>(y - region.top) * image.width * 4,
                            reader.row.data() + static_cast<std::size_t>(region.left) * 4,
                            static_cast<std::size_t>(image.width) * 4);
        }
    }
    png_read_end(reader.png, nullptr);
    return true;
}
bool read_png(const std::filesystem::path& path, PngReader& reader, ImageResult& r, DecodeMode mode)
{
    if (reader.file)
    {
        // Keep the same open file across both passes: an atomic asset
        // replacement must not pair old bounds with a different new PNG.
        png_destroy_read_struct(&reader.png, &reader.info, nullptr);
        if (std::fseek(reader.file, 0, SEEK_SET) != 0)
        {
            r.image = {};
            r.region = {};
            r.error = path.string() + ": cannot rewind PNG";
            return false;
        }
    }
    else
        reader.file = std::fopen(path.c_str(), "rb");
    if (!reader.file)
    {
        r.image = {};
        r.region = {};
        r.error = path.string() + ": cannot open PNG";
        return false;
    }
    reader.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &reader, png_error, png_warning);
    if (reader.png)
        reader.info = png_create_info_struct(reader.png);
    if (!reader.info)
    {
        r.image = {};
        r.region = {};
        r.error = path.string() + ": cannot allocate PNG decoder";
        return false;
    }
    if (!decode_png(reader, r.image, mode, r.region))
    {
        r.image = {};
        r.error = path.string() + ": " + reader.error;
        r.region = {};
        return false;
    }
    return true;
}
} // namespace
DecodedImageRgba8::DecodedImageRgba8() : pixels(&kDecodedPixels)
{
}
ImageResult load_png(const std::filesystem::path& path)
{
    ImageResult r;
    PngReader reader;
    r.ok = read_png(path, reader, r, DecodeMode::Full);
    return r;
}
ImageResult load_cropped_png(const std::filesystem::path& path)
{
    ImageResult r;
    Bounds bounds;
    PngReader reader;
    if (!read_png(path, reader, r, DecodeMode::Bounds))
        return r;
    if (!r.image.pixels.empty())
        for (int y = 0; y < r.image.height; ++y)
            reader.bounds.scan(r.image.pixels.data() +
                                   static_cast<std::size_t>(y) * r.image.width * 4,
                               r.image.width, y);
    bounds = reader.bounds;
    const int source_width = r.image.width, source_height = r.image.height;
    if (bounds.right == 0)
    {
        r.image = {};
        r.image.width = r.image.height = 1;
        r.image.pixels.resize(4);
        r.region = {source_width, source_height, 0, 0};
        r.ok = true;
        return r;
    }
    // One texel preserves the full bilinear footprint; do not extend original
    // edges, whose CLAMP_TO_EDGE behaviour must remain unchanged.
    const int left = std::max(0, bounds.left - 1), top = std::max(0, bounds.top - 1);
    const int right = std::min(source_width, bounds.right + 1);
    const int bottom = std::min(source_height, bounds.bottom + 1);
    r.region = {source_width, source_height, left, top};
    if (!r.image.pixels.empty())
    {
        if (left != 0 || top != 0 || right != source_width || bottom != source_height)
        {
            DecodedImageRgba8 cropped;
            cropped.width = right - left;
            cropped.height = bottom - top;
            cropped.pixels.resize(static_cast<std::size_t>(cropped.width) * cropped.height * 4);
            for (int y = 0; y < cropped.height; ++y)
                std::memcpy(cropped.pixels.data() + static_cast<std::size_t>(y) * cropped.width * 4,
                            r.image.pixels.data() +
                                (static_cast<std::size_t>(y + top) * source_width + left) * 4,
                            static_cast<std::size_t>(cropped.width) * 4);
            r.image = std::move(cropped);
        }
        r.ok = true;
        return r;
    }
    r.image.width = right - left;
    r.image.height = bottom - top;
    r.ok = read_png(path, reader, r, DecodeMode::Crop);
    return r;
}
std::string write_png(const std::filesystem::path& path, ImageViewRgba8 image)
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
