#include <nekomote/asset_io/asset_migration.hpp>

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: nekomote_model_convert SOURCE.nkm.json NEW.nkm\n");
        return 2;
    }
    const auto error = nekomote::asset_io::convert_model_json(argv[1], argv[2]);
    if (!error.empty())
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::puts("{\"ok\":true}");
    return 0;
}
