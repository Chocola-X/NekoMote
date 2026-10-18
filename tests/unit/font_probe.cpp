#include <nekomote/ui/font.hpp>

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
std::size_t mapped_font_files()
{
    std::ifstream maps("/proc/self/maps");
    std::set<std::string> paths;
    for (std::string line; std::getline(maps, line);)
    {
        const auto path_start = line.find('/');
        if (path_start == std::string::npos)
            continue;
        const auto path = line.substr(path_start);
        if (path.ends_with(".ttf") || path.ends_with(".ttc") || path.ends_with(".otf"))
            paths.insert(path);
    }
    return paths.size();
}
} // namespace

int main(int argc, char** argv)
{
    nekomote::ui::FontMetrics font;
    if (!font.init("", 36))
    {
        std::cerr << font.last_error() << '\n';
        return 1;
    }
    std::vector<float> vertices, uvs;
    if (argc == 3 && std::string(argv[1]) == "--inventory")
    {
        std::cout << mapped_font_files() << ' ';
        if (!font.prepare_text(argv[2]))
            return 3;
        std::cout << mapped_font_files() << ' ';
        if (!font.prepare_text(argv[2]))
            return 3;
        std::cout << mapped_font_files() << '\n';
        return 0;
    }
    if (argc > 1)
    {
        std::vector<unsigned char> pixels;
        nekomote::ui::Glyph result{};
        font.on_glyph_added = [&](const nekomote::ui::Glyph& glyph, const unsigned char* bitmap)
        {
            result = glyph;
            pixels.assign(bitmap, bitmap + glyph.atlas_w * glyph.atlas_h);
        };
        if (!font.prepare_text(argv[1]))
        {
            std::cerr << font.last_error() << '\n';
            return 3;
        }
        font.draw_text(vertices, uvs, 0, 0, argv[1], 16);
        unsigned long hash = 0;
        for (const auto pixel : pixels)
            hash = hash * 31 + pixel;
        std::cout << result.atlas_w << ' ' << result.atlas_h << ' ' << hash << ' '
                  << font.text_width(argv[1], 16) << '\n';
        return 0;
    }
    if (!font.prepare_text("Hello"))
        return 3;
    font.draw_text(vertices, uvs, 0, 0, "Hello", 16);
    return font.last_error().empty() && font.text_width("Hello", 16) > 0 && vertices.size() == 60 &&
                   uvs.size() == 60
               ? 0
               : 2;
}
