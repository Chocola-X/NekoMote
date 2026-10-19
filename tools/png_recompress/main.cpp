#include <nekomote/asset_io/image.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 4 || (std::string(argv[1]) != "recompress" && std::string(argv[1]) != "strip"))
    {
        std::fprintf(stderr, "usage: nekomote_png_recompress recompress|strip SOURCE DEST\n");
        return 2;
    }
    const std::filesystem::path source(argv[2]), destination(argv[3]);
    const auto error = std::string(argv[1]) == "recompress"
                           ? nekomote::asset_io::recompress_png(source, destination)
                           : nekomote::asset_io::strip_png_metadata(source, destination);
    if (!error.empty())
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        std::printf("{\"ok\":false,\"error\":\"%s\"}\n", error.c_str());
        return 1;
    }
    std::error_code code;
    const auto before = std::filesystem::file_size(source, code);
    if (code)
    {
        std::fprintf(stderr, "cannot stat source: %s\n", source.string().c_str());
        return 1;
    }
    const auto after = std::filesystem::file_size(destination, code);
    if (code)
    {
        std::fprintf(stderr, "cannot stat output: %s\n", destination.string().c_str());
        return 1;
    }
    std::printf("{\"ok\":true,\"source_bytes\":%llu,\"output_bytes\":%llu}\n",
                static_cast<unsigned long long>(before), static_cast<unsigned long long>(after));
    return 0;
}
