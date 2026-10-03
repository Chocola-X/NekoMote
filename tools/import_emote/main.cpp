// Offline structural importer and current asset bundle compiler.

#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/import_emote.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

void print_usage()
{
    std::fprintf(stderr, "usage: nekomote_import <model.json> <model.resx.json> "
                         "--output <new-dir> [--resource-root <dir>]\n");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        print_usage();
        return 1;
    }

    nekomote::asset_io::ImportOptions opts;
    opts.json_path = argv[1];
    opts.resx_path = argv[2];
    opts.resource_root = opts.json_path.parent_path();
    std::filesystem::path output, audit_report;

    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--texture" && i + 1 < argc)
        {
            const std::string mapping = argv[++i];
            const auto eq = mapping.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == mapping.size())
            {
                print_usage();
                return 1;
            }
            opts.external_textures.emplace(mapping.substr(0, eq), mapping.substr(eq + 1));
        }
        else if (a == "--audit-report" && i + 1 < argc)
        {
            audit_report = argv[++i];
        }
        else if (a == "--output" && i + 1 < argc)
        {
            output = argv[++i];
        }
        else if (a == "--resource-root" && i + 1 < argc)
        {
            opts.resource_root = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (output.empty() && audit_report.empty())
    {
        print_usage();
        return 1;
    }

    nekomote::asset_io::ImportResult result = nekomote::asset_io::import_emote(opts);
    const auto& r = result.report;

    std::printf("=== NekoMote import report ===\n");
    std::printf("ok: %s\n", result.ok ? "yes" : "no");
    std::printf("spec: %s  version: %.2f\n", result.asset.spec.c_str(),
                static_cast<double>(result.asset.version));
    std::printf("screen: %dx%d origin (%d, %d)\n", result.asset.screen_width,
                result.asset.screen_height, result.asset.screen_origin_x,
                result.asset.screen_origin_y);
    std::printf("parts: %d  layers: %d  frames: %d\n", r.parts_count, r.layers_count,
                r.frames_count);
    std::printf("icons: %d  textures: %d  variables: %d\n", r.icons_count, r.textures_count,
                r.variables_count);
    std::printf("bp_nonnull: %d  cc_nonnull: %d\n", result.asset.bp_nonnull,
                result.asset.cc_nonnull);
    std::printf("parts_order: [");
    for (std::size_t i = 0; i < result.asset.parts_order.size(); ++i)
    {
        std::printf("%s%s", i ? "," : "", result.asset.parts_order[i].c_str());
    }
    std::printf("]\n");
    if (!r.texture_path.empty())
    {
        std::printf("texture_path: %s\n", r.texture_path.c_str());
    }
    for (const std::string& w : r.warnings)
    {
        std::printf("warning: %s\n", w.c_str());
    }
    for (const std::string& e : r.errors)
    {
        std::printf("error: %s\n", e.c_str());
    }

    if (!audit_report.empty())
    {
        nlohmann::json report{{"ok", result.ok},
                              {"errors", r.errors},
                              {"unsupported_fields", r.unsupported_fields},
                              {"warnings", r.warnings},
                              {"parts", r.parts_count},
                              {"layers", r.layers_count},
                              {"frames", r.frames_count},
                              {"icons", r.icons_count},
                              {"textures", r.textures_count},
                              {"variables", r.variables_count},
                              {"bp", result.asset.bp_nonnull},
                              {"cc", result.asset.cc_nonnull}};
        std::ofstream file(audit_report);
        file << report.dump(2) << '\n';
        file.close();
        if (!file)
        {
            std::fprintf(stderr, "cannot write audit report\n");
            return 2;
        }
    }
    if (!result.ok)
        return 2;
    if (output.empty())
        return 0;
    const auto errors = nekomote::asset_io::write_bundle(result, opts.resource_root, output);
    for (const auto& e : errors)
        std::fprintf(stderr, "error: %s\n", e.c_str());
    return errors.empty() ? 0 : 2;
}
