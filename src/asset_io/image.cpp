#include <nekomote/asset_io/image.hpp>

#include <png.h>
#include <stb_image_write.h>
#include <sys/mman.h>
#include <zlib.h>

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

namespace
{
struct RawPngLayout
{
    int width{0}, height{0};
    int color_type{0};
    std::size_t stride{0}; // Width * channels, without the filter byte.
    std::vector<png_byte> pixels;
    int channels() const noexcept
    {
        return color_type == PNG_COLOR_TYPE_RGBA ? 4 : 3;
    }
};

int image_channels(int color_type)
{
    return color_type == PNG_COLOR_TYPE_RGBA ? 4 : 3;
}

// Reads the encoded colour layout without any channel transform so the
// re-encoded file keeps the source colour type (RGB stays 3-channel).
bool decode_raw_layout(PngReader& reader, RawPngLayout& image)
{
    if (setjmp(png_jmpbuf(reader.png)))
        return false;
    png_init_io(reader.png, reader.file);
    png_read_info(reader.png, reader.info);
    const auto width = png_get_image_width(reader.png, reader.info);
    const auto height = png_get_image_height(reader.png, reader.info);
    const auto color = png_get_color_type(reader.png, reader.info);
    const auto depth = png_get_bit_depth(reader.png, reader.info);
    if (width == 0 || height == 0 || width > 32768 || height > 32768)
        png_error(reader.png, "invalid or unsupported PNG dimensions");
    if (depth != 8 || (color != PNG_COLOR_TYPE_RGB && color != PNG_COLOR_TYPE_RGBA))
        png_error(reader.png, "only 8-bit non-palette RGB/RGBA PNGs are supported");
    if (png_get_valid(reader.png, reader.info, PNG_INFO_tRNS) != 0)
        png_error(reader.png, "tRNS transparency is not supported; flatten alpha first");
    png_set_interlace_handling(reader.png);
    png_read_update_info(reader.png, reader.info);
    const auto channels = png_get_channels(reader.png, reader.info);
    image.stride = static_cast<std::size_t>(width) * channels;
    if (channels != image_channels(color) ||
        png_get_rowbytes(reader.png, reader.info) != image.stride)
        png_error(reader.png, "unexpected raw PNG layout");
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.color_type = color;
    image.pixels.resize(image.stride * height);
    reader.rows.resize(height);
    for (std::size_t y = 0; y < height; ++y)
        reader.rows[y] = image.pixels.data() + y * image.stride;
    png_read_image(reader.png, reader.rows.data());
    png_read_end(reader.png, nullptr);
    return true;
}

std::string read_raw_png(const std::filesystem::path& path, RawPngLayout& image)
{
    PngReader reader;
    reader.file = std::fopen(path.c_str(), "rb");
    if (!reader.file)
        return "cannot open PNG: " + path.string();
    reader.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &reader, png_error, png_warning);
    if (reader.png)
        reader.info = png_create_info_struct(reader.png);
    if (!reader.info)
        return "cannot allocate PNG decoder";
    if (!decode_raw_layout(reader, image))
        return path.string() + ": " + reader.error;
    return {};
}

png_byte paeth_predictor(png_byte a, png_byte b, png_byte c)
{
    const auto p = int(a) + int(b) - int(c);
    const auto pa = std::abs(p - int(a)), pb = std::abs(p - int(b)), pc = std::abs(p - int(c));
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

void filter_row(int type, const png_byte* row, const png_byte* prev, std::size_t n, int bpp,
                png_byte* out)
{
    out[0] = static_cast<png_byte>(type);
    png_byte* dst = out + 1;
    switch (type)
    {
        case 0:
            std::memcpy(dst, row, n);
            break;
        case 1:
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = static_cast<png_byte>(
                    row[i] - (i >= static_cast<std::size_t>(bpp) ? row[i - bpp] : 0));
            break;
        case 2:
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = static_cast<png_byte>(row[i] - prev[i]);
            break;
        case 3:
            for (std::size_t i = 0; i < n; ++i)
            {
                const int a = i >= static_cast<std::size_t>(bpp) ? row[i - bpp] : 0;
                dst[i] = static_cast<png_byte>(row[i] - ((a + prev[i]) >> 1));
            }
            break;
        default:
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto a =
                    static_cast<png_byte>(i >= static_cast<std::size_t>(bpp) ? row[i - bpp] : 0);
                const auto c =
                    static_cast<png_byte>(i >= static_cast<std::size_t>(bpp) ? prev[i - bpp] : 0);
                dst[i] = static_cast<png_byte>(row[i] - paeth_predictor(a, prev[i], c));
            }
            break;
    }
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_chunk(std::vector<std::uint8_t>& out, const char* type, const std::uint8_t* data,
                  std::size_t size)
{
    append_be32(out, static_cast<std::uint32_t>(size));
    const auto* first = reinterpret_cast<const std::uint8_t*>(type);
    out.insert(out.end(), first, first + 4);
    if (size)
        out.insert(out.end(), data, data + size);
    auto crc = crc32(crc32(0L, Z_NULL, 0), first, 4);
    if (size)
        crc = crc32(crc, data, static_cast<uInt>(size));
    append_be32(out, static_cast<std::uint32_t>(crc));
}

std::string write_bytes(const std::filesystem::path& path, const std::uint8_t* data,
                        std::size_t size)
{
    auto* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return "cannot create file: " + path.string();
    const auto written = std::fwrite(data, 1, size, file);
    const auto closed = std::fclose(file);
    if (written != size || closed != 0)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return "cannot write file: " + path.string();
    }
    return {};
}

// Two independent checks before publishing: the encoded layout must decode
// back to identical bytes, and the runtime RGBA decode path must agree with
// the source for both pixels and crop region.
std::string verify_reencoded_png(const std::filesystem::path& source,
                                 const std::filesystem::path& output, const RawPngLayout& original)
{
    RawPngLayout recheck;
    if (auto error = read_raw_png(output, recheck); !error.empty())
        return "re-encoded PNG failed verification: " + error;
    if (recheck.width != original.width || recheck.height != original.height ||
        recheck.color_type != original.color_type || recheck.pixels != original.pixels)
        return "re-encoded PNG differs from source: " + source.string();
    const auto before = load_png(source);
    const auto after = load_png(output);
    if (!before.ok || !after.ok)
        return "re-encoded PNG failed runtime decode: " + source.string();
    if (after.image.width != before.image.width || after.image.height != before.image.height ||
        after.image.pixels != before.image.pixels ||
        after.region.source_width != before.region.source_width ||
        after.region.source_height != before.region.source_height ||
        after.region.left != before.region.left || after.region.top != before.region.top)
        return "re-encoded PNG changed runtime decode: " + source.string();
    return {};
}
} // namespace

std::string recompress_png(const std::filesystem::path& source,
                           const std::filesystem::path& destination)
{
    RawPngLayout image;
    if (auto error = read_raw_png(source, image); !error.empty())
        return error;
    const auto bpp = image.channels();
    const auto row_bytes = image.stride + 1;
    std::vector<png_byte> filtered(static_cast<std::size_t>(image.height) * row_bytes);
    std::vector<png_byte> candidate(row_bytes), winner(row_bytes);
    std::vector<png_byte> trial(compressBound(row_bytes));
    std::vector<png_byte> zero(image.stride, 0);
    for (int y = 0; y < image.height; ++y)
    {
        const auto* row = image.pixels.data() + static_cast<std::size_t>(y) * image.stride;
        const auto* prev = y ? row - image.stride : zero.data();
        auto best = static_cast<uLong>(trial.size()) + 1;
        for (int type = 0; type < 5; ++type)
        {
            filter_row(type, row, prev, image.stride, bpp, candidate.data());
            auto size = static_cast<uLongf>(trial.size());
            if (compress2(trial.data(), &size, candidate.data(),
                          static_cast<uLongf>(candidate.size()), 9) != Z_OK)
                return "zlib compression failed: " + source.string();
            if (size < best)
            {
                best = size;
                std::memcpy(winner.data(), candidate.data(), row_bytes);
            }
        }
        std::memcpy(filtered.data() + static_cast<std::size_t>(y) * row_bytes, winner.data(),
                    row_bytes);
    }
    std::vector<std::uint8_t> idat(compressBound(filtered.size()));
    auto idat_size = static_cast<uLongf>(idat.size());
    if (compress2(idat.data(), &idat_size, filtered.data(), static_cast<uLongf>(filtered.size()),
                  9) != Z_OK)
        return "zlib compression failed: " + source.string();
    idat.resize(idat_size);

    std::vector<std::uint8_t> file;
    file.reserve(64 + idat.size());
    static const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    file.insert(file.end(), signature, signature + 8);
    std::uint8_t ihdr[13]{};
    ihdr[0] = static_cast<std::uint8_t>(image.width >> 24);
    ihdr[1] = static_cast<std::uint8_t>(image.width >> 16);
    ihdr[2] = static_cast<std::uint8_t>(image.width >> 8);
    ihdr[3] = static_cast<std::uint8_t>(image.width);
    ihdr[4] = static_cast<std::uint8_t>(image.height >> 24);
    ihdr[5] = static_cast<std::uint8_t>(image.height >> 16);
    ihdr[6] = static_cast<std::uint8_t>(image.height >> 8);
    ihdr[7] = static_cast<std::uint8_t>(image.height);
    ihdr[8] = 8;
    ihdr[9] = static_cast<std::uint8_t>(image.color_type);
    append_chunk(file, "IHDR", ihdr, sizeof(ihdr));
    append_chunk(file, "IDAT", idat.data(), idat.size());
    append_chunk(file, "IEND", nullptr, 0);

    auto temporary = destination;
    temporary += ".tmp-recompress";
    if (auto error = write_bytes(temporary, file.data(), file.size()); !error.empty())
        return error;
    if (auto error = verify_reencoded_png(source, temporary, image); !error.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return error;
    }
    std::error_code code;
    std::filesystem::rename(temporary, destination, code);
    if (code)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return "cannot publish re-encoded PNG: " + destination.string();
    }
    return {};
}

std::string strip_png_metadata(const std::filesystem::path& source,
                               const std::filesystem::path& destination)
{
    auto* opened = std::fopen(source.c_str(), "rb");
    if (!opened)
        return "cannot open PNG: " + source.string();
    std::vector<std::uint8_t> data;
    {
        std::fseek(opened, 0, SEEK_END);
        const auto size = std::ftell(opened);
        if (size <= 0 || std::fseek(opened, 0, SEEK_SET) != 0)
        {
            std::fclose(opened);
            return "cannot read PNG: " + source.string();
        }
        data.resize(static_cast<std::size_t>(size));
        if (std::fread(data.data(), 1, data.size(), opened) != data.size())
        {
            std::fclose(opened);
            return "cannot read PNG: " + source.string();
        }
        std::fclose(opened);
    }
    static const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (data.size() < 8 || std::memcmp(data.data(), signature, 8) != 0)
        return "not a PNG file: " + source.string();
    std::vector<std::uint8_t> out(signature, signature + 8);
    bool header = false, pixels = false, trailer = false, pixels_closed = false;
    std::size_t position = 8;
    while (position < data.size())
    {
        if (data.size() - position < 12)
            return "truncated chunk in PNG: " + source.string();
        const auto length = (std::uint32_t(data[position]) << 24) |
                            (std::uint32_t(data[position + 1]) << 16) |
                            (std::uint32_t(data[position + 2]) << 8) | data[position + 3];
        if (length > data.size() - position - 12)
            return "truncated chunk in PNG: " + source.string();
        const auto* type = data.data() + position + 4;
        const auto* body = data.data() + position + 8;
        auto crc = crc32(crc32(0L, Z_NULL, 0), type, 4);
        if (length)
            crc = crc32(crc, body, static_cast<uInt>(length));
        const auto stored = (std::uint32_t(body[length]) << 24) |
                            (std::uint32_t(body[length + 1]) << 16) |
                            (std::uint32_t(body[length + 2]) << 8) | body[length + 3];
        if (crc != stored)
            return "chunk CRC mismatch in PNG: " + source.string();
        const bool is_header = !std::memcmp(type, "IHDR", 4);
        const bool is_pixels = !std::memcmp(type, "IDAT", 4);
        const bool is_trailer = !std::memcmp(type, "IEND", 4);
        if (is_header)
        {
            if (header || position != 8)
                return "misplaced IHDR in PNG: " + source.string();
            header = true;
        }
        else if (!header)
            return "PNG does not start with IHDR: " + source.string();
        if (is_pixels)
        {
            if (pixels_closed)
                return "IDAT after non-pixel chunks: " + source.string();
            pixels = true;
        }
        else if (pixels)
        {
            pixels_closed = true;
        }
        if (is_trailer && position + 12 + length != data.size())
            return "trailing data after IEND: " + source.string();
        // Keep only the chunks libpng needs to reproduce the pixels; provenance
        // (iTXt/tEXt/zTXt) and colour-management hints are dropped because the
        // runtime decoder never consumes them.
        const bool keep = is_header || is_pixels || is_trailer || !std::memcmp(type, "PLTE", 4) ||
                          !std::memcmp(type, "tRNS", 4);
        if (keep)
            out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(position),
                       data.begin() + static_cast<std::ptrdiff_t>(position + 12 + length));
        position += 12 + length;
        if (is_trailer)
        {
            trailer = true;
            break;
        }
    }
    if (!header || !pixels || !trailer)
        return "incomplete PNG structure: " + source.string();

    auto temporary = destination;
    temporary += ".tmp-strip";
    if (auto error = write_bytes(temporary, out.data(), out.size()); !error.empty())
        return error;
    const auto before = load_png(source);
    const auto after = load_png(temporary);
    if (!before.ok || !after.ok || after.image.width != before.image.width ||
        after.image.height != before.image.height || after.image.pixels != before.image.pixels ||
        after.region.source_width != before.region.source_width ||
        after.region.source_height != before.region.source_height ||
        after.region.left != before.region.left || after.region.top != before.region.top)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return "stripped PNG failed verification: " + source.string();
    }
    std::error_code code;
    std::filesystem::rename(temporary, destination, code);
    if (code)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return "cannot publish stripped PNG: " + destination.string();
    }
    return {};
}
} // namespace nekomote::asset_io
