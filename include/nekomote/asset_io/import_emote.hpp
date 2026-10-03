#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nekomote/model/asset.hpp>

namespace nekomote::asset_io {

struct ImportOptions {
    std::filesystem::path json_path;        // model .json
    std::filesystem::path resx_path;        // model .resx.json
    std::filesystem::path resource_root;    // dir resolving texture paths
};

struct ImportReport {
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    int parts_count{0};
    int layers_count{0};
    int frames_count{0};
    int icons_count{0};
    int textures_count{0};
    int variables_count{0};
    std::string texture_path;  // resolved primary texture path
};

struct ImportResult {
    model::ModelAsset asset;
    ImportReport report;
    bool ok{false};
};

// Parse an E-mote PSB JSON + resx into a ModelAsset. Never throws; failures
// land in ImportReport.errors and ok=false.
ImportResult import_emote(const ImportOptions& opts);

}  // namespace nekomote::asset_io
