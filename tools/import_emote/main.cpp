// nekomote_import: offline E-mote import CLI.
//
// Usage:
//   nekomote_import <model.json> <model.resx.json> [--resource-root <dir>]
//
// Parses the source E-mote JSON + resx into a ModelAsset and prints a capability
// report (counts, texture path, structure summary). Future versions will write a
// normalized asset bundle to an output directory.

#include <nekomote/asset_io/import_emote.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

void print_usage() {
    std::fprintf(stderr,
                 "usage: nekomote_import <model.json> <model.resx.json> "
                 "[--resource-root <dir>]\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    nekomote::asset_io::ImportOptions opts;
    opts.json_path = argv[1];
    opts.resx_path = argv[2];

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--resource-root" && i + 1 < argc) {
            opts.resource_root = argv[++i];
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    nekomote::asset_io::ImportResult result = nekomote::asset_io::import_emote(opts);
    const auto& r = result.report;

    std::printf("=== NekoMote import report ===\n");
    std::printf("ok: %s\n", result.ok ? "yes" : "no");
    std::printf("spec: %s  version: %.2f\n", result.asset.spec.c_str(), static_cast<double>(result.asset.version));
    std::printf("screen: %dx%d origin (%d, %d)\n",
                result.asset.screen_width, result.asset.screen_height,
                result.asset.screen_origin_x, result.asset.screen_origin_y);
    std::printf("parts: %d  layers: %d  frames: %d\n", r.parts_count, r.layers_count, r.frames_count);
    std::printf("icons: %d  textures: %d  variables: %d\n", r.icons_count, r.textures_count, r.variables_count);
    std::printf("bp_nonnull: %d  cc_nonnull: %d\n", result.asset.bp_nonnull, result.asset.cc_nonnull);
    std::printf("parts_order: [");
    for (std::size_t i = 0; i < result.asset.parts_order.size(); ++i) {
        std::printf("%s%s", i ? "," : "", result.asset.parts_order[i].c_str());
    }
    std::printf("]\n");
    if (!r.texture_path.empty()) {
        std::printf("texture_path: %s\n", r.texture_path.c_str());
    }
    for (const std::string& w : r.warnings) {
        std::printf("warning: %s\n", w.c_str());
    }
    for (const std::string& e : r.errors) {
        std::printf("error: %s\n", e.c_str());
    }

    return result.ok ? 0 : 2;
}
