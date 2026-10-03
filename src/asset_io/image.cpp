#include <nekomote/asset_io/image.hpp>

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <memory>

namespace nekomote::asset_io
{
ImageResult load_png(const std::filesystem::path& path)
{
    ImageResult r;
    int channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path.string().c_str(), &r.image.width, &r.image.height, &channels, 4),
        stbi_image_free);
    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        r.error = path.string() + ": " + (reason ? reason : "PNG decode failed");
        return r;
    }
    const auto size =
        static_cast<std::size_t>(r.image.width) * static_cast<std::size_t>(r.image.height) * 4;
    r.image.pixels.assign(pixels.get(), pixels.get() + size);
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
