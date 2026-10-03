// E-mote PSB JSON importer. Parses the source JSON + resx into a ModelAsset.
//
// Field semantics and evidence: docs/p1-handoff.md. The importer preserves the
// distinction between null / missing / zero via std::optional where it matters
// (bp, cc, opa, color, bm). Unknown fields are skipped and recorded as warnings
// only when they appear significant; otherwise ignored to keep noise low.

#include <nekomote/asset_io/import_emote.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <utility>

namespace nekomote::asset_io {

namespace {

using json = nlohmann::json;

bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size < 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(out.data(), static_cast<std::streamsize>(out.size()));
    return f.good() || f.eof();
}

float to_f(const json& v) {
    return static_cast<float>(v.get<double>());
}

int get_int(const json& obj, const char* key, int def = 0) {
    if (obj.contains(key) && !obj[key].is_null() && obj[key].is_number_integer()) {
        return obj[key].get<int>();
    }
    return def;
}

std::uint64_t get_uint64(const json& obj, const char* key, std::uint64_t def = 0) {
    if (obj.contains(key) && !obj[key].is_null() && obj[key].is_number()) {
        return obj[key].get<std::uint64_t>();
    }
    return def;
}

std::optional<std::array<float, 32>> parse_bp(const json& mesh, const char* key) {
    if (!mesh.contains(key) || mesh[key].is_null()) {
        return std::nullopt;
    }
    const json& arr = mesh[key];
    if (!arr.is_array() || arr.size() != 32) {
        return std::nullopt;
    }
    std::array<float, 32> out{};
    for (std::size_t i = 0; i < 32; ++i) {
        out[i] = to_f(arr[i]);
    }
    return out;
}

model::MeshData parse_mesh(const json& content) {
    model::MeshData m;
    if (!content.contains("mesh") || content["mesh"].is_null()) {
        return m;
    }
    const json& mesh = content["mesh"];
    m.bp = parse_bp(mesh, "bp");
    m.cc = parse_bp(mesh, "cc");
    return m;
}

model::FrameContent parse_content(const json& frame) {
    model::FrameContent c;
    if (!frame.contains("content") || frame["content"].is_null()) {
        return c;
    }
    const json& content = frame["content"];

    if (content.contains("coord") && content["coord"].is_array()) {
        const json& coord = content["coord"];
        for (std::size_t i = 0; i < 3 && i < coord.size(); ++i) {
            c.coord[i] = to_f(coord[i]);
        }
    }
    if (content.contains("icon") && !content["icon"].is_null()) {
        c.icon = content["icon"].get<std::string>();
    }
    if (content.contains("src") && !content["src"].is_null()) {
        c.src = content["src"].get<std::string>();
    }
    c.mask = get_uint64(content, "mask");
    if (content.contains("opa") && !content["opa"].is_null()) {
        c.opa = to_f(content["opa"]);
    }
    if (content.contains("color") && !content["color"].is_null() && content["color"].is_number()) {
        c.color = content["color"].get<std::uint32_t>();
    }
    c.bm = get_int(content, "bm");
    c.mesh = parse_mesh(content);
    return c;
}

model::LayerFrame parse_frame(const json& fr) {
    model::LayerFrame out;
    out.time = get_int(fr, "time");
    out.type = get_int(fr, "type");
    out.content = parse_content(fr);
    return out;
}

std::vector<model::ParamBinding> parse_parameter(const json& layer) {
    std::vector<model::ParamBinding> out;
    if (!layer.contains("parameter") || !layer["parameter"].is_array()) {
        return out;
    }
    for (const json& p : layer["parameter"]) {
        model::ParamBinding b;
        if (p.contains("id") && !p["id"].is_null()) {
            b.id = p["id"].get<std::string>();
        }
        if (p.contains("rangeBegin") && !p["rangeBegin"].is_null()) {
            b.range_begin = to_f(p["rangeBegin"]);
        }
        if (p.contains("rangeEnd") && !p["rangeEnd"].is_null()) {
            b.range_end = to_f(p["rangeEnd"]);
        }
        b.division = get_int(p, "division");
        b.discretization = get_int(p, "discretization");
        if (p.contains("enabled") && !p["enabled"].is_null() && p["enabled"].is_number_integer()) {
            b.enabled = p["enabled"].get<int>() != 0;
        }
        out.push_back(std::move(b));
    }
    return out;
}

struct LayerStats {
    int layers{0};
    int frames{0};
    int bp_nonnull{0};
    int cc_nonnull{0};
};

model::Layer parse_layer(const json& layer, LayerStats& stats) {
    model::Layer out;
    stats.layers += 1;

    if (layer.contains("label") && !layer["label"].is_null()) {
        out.label = layer["label"].get<std::string>();
    }
    out.coordinate = get_int(layer, "coordinate");
    out.mesh_combine = get_int(layer, "meshCombine");
    out.mesh_division = get_int(layer, "meshDivision");
    out.mesh_transform = get_int(layer, "meshTransform");
    out.parameterize = get_int(layer, "parameterize");
    out.stencil_type = get_int(layer, "stencilType");
    out.inherit_mask = get_uint64(layer, "inheritMask");
    if (layer.contains("transformOrder") && layer["transformOrder"].is_array()) {
        for (const json& t : layer["transformOrder"]) {
            if (t.is_number_integer()) {
                out.transform_order.push_back(t.get<int>());
            }
        }
    }
    out.parameter = parse_parameter(layer);

    if (layer.contains("frameList") && layer["frameList"].is_array()) {
        for (const json& fr : layer["frameList"]) {
            out.frame_list.push_back(parse_frame(fr));
            stats.frames += 1;
            if (out.frame_list.back().content.mesh.bp.has_value()) {
                stats.bp_nonnull += 1;
            }
            if (out.frame_list.back().content.mesh.cc.has_value()) {
                stats.cc_nonnull += 1;
            }
        }
    }

    if (layer.contains("children") && layer["children"].is_array()) {
        for (const json& child : layer["children"]) {
            out.children.push_back(parse_layer(child, stats));
        }
    }
    return out;
}

std::vector<model::VariableDef> parse_variable_list(const json& metadata, const char* key) {
    std::vector<model::VariableDef> out;
    if (!metadata.contains(key) || !metadata[key].is_array()) {
        return out;
    }
    for (const json& v : metadata[key]) {
        model::VariableDef def;
        if (v.contains("label")) {
            def.label = v["label"].get<std::string>();
        }
        if (v.contains("frameList") && v["frameList"].is_array()) {
            for (const json& fr : v["frameList"]) {
                int value = get_int(fr, "frame");
                std::string label;
                if (fr.contains("label") && !fr["label"].is_null()) {
                    label = fr["label"].get<std::string>();
                }
                def.frames.emplace_back(value, std::move(label));
            }
        }
        out.push_back(std::move(def));
    }
    return out;
}

std::unordered_map<std::string, model::IconRect> parse_icons(const json& source) {
    std::unordered_map<std::string, model::IconRect> out;
    if (!source.contains("tex") || !source["tex"].is_object()) {
        return out;
    }
    const json& tex = source["tex"];
    if (!tex.contains("icon") || !tex["icon"].is_object()) {
        return out;
    }
    for (auto it = tex["icon"].begin(); it != tex["icon"].end(); ++it) {
        const json& e = it.value();
        if (!e.is_object()) {
            continue;
        }
        model::IconRect r;
        if (e.contains("left")) {
            r.left = to_f(e["left"]);
        }
        if (e.contains("top")) {
            r.top = to_f(e["top"]);
        }
        if (e.contains("width")) {
            r.width = to_f(e["width"]);
        }
        if (e.contains("height")) {
            r.height = to_f(e["height"]);
        }
        if (e.contains("originX")) {
            r.origin_x = to_f(e["originX"]);
        }
        if (e.contains("originY")) {
            r.origin_y = to_f(e["originY"]);
        }
        r.attr = get_int(e, "attr");
        if (e.contains("metadata") && e["metadata"].is_object()) {
            r.zorder = get_int(e["metadata"], "zorder");
        }
        out[it.key()] = r;
    }
    return out;
}

// Parse resx.json Resources map: {"0": "path/to.png"}.
std::unordered_map<std::string, std::string> parse_resx_resources(const json& resx) {
    std::unordered_map<std::string, std::string> out;
    if (!resx.contains("Resources") || !resx["Resources"].is_object()) {
        return out;
    }
    for (auto it = resx["Resources"].begin(); it != resx["Resources"].end(); ++it) {
        if (it.value().is_string()) {
            out[it.key()] = it.value().get<std::string>();
        }
    }
    return out;
}

std::vector<model::TextureDef> parse_textures(const json& source,
                                              const std::unordered_map<std::string, std::string>& resx_resources,
                                              ImportReport& report) {
    std::vector<model::TextureDef> out;
    if (!source.contains("tex") || !source["tex"].is_object()) {
        return out;
    }
    const json& tex = source["tex"];
    if (!tex.contains("texture") || !tex["texture"].is_object()) {
        return out;
    }
    const json& t = tex["texture"];
    model::TextureDef def;
    def.name = "tex";
    def.width = get_int(t, "width");
    def.height = get_int(t, "height");
    if (t.contains("type") && t["type"].is_string()) {
        def.format = t["type"].get<std::string>();
    }
    // Resolve path: texture.pixel = "#resource#N" -> resx Resources["N"].
    if (t.contains("pixel") && t["pixel"].is_string()) {
        std::string pixel = t["pixel"].get<std::string>();
        const std::string prefix = "#resource#";
        if (pixel.rfind(prefix, 0) == 0) {
            std::string key = pixel.substr(prefix.size());
            auto it = resx_resources.find(key);
            if (it != resx_resources.end()) {
                def.path = it->second;
                report.texture_path = def.path;
            }
        }
    }
    out.push_back(std::move(def));
    return out;
}

}  // namespace

ImportResult import_emote(const ImportOptions& opts) {
    ImportResult result;

    std::string json_str;
    if (!read_file(opts.json_path, json_str)) {
        result.report.errors.push_back("failed to read model json: " + opts.json_path.string());
        return result;
    }

    json d;
    try {
        d = json::parse(json_str);
    } catch (const std::exception& e) {
        result.report.errors.push_back(std::string("json parse error: ") + e.what());
        return result;
    }

    model::ModelAsset& asset = result.asset;

    if (d.contains("spec")) {
        asset.spec = d["spec"].get<std::string>();
    }
    if (d.contains("version")) {
        asset.version = to_f(d["version"]);
    }
    if (d.contains("screenSize") && d["screenSize"].is_object()) {
        const json& s = d["screenSize"];
        asset.screen_width = get_int(s, "width");
        asset.screen_height = get_int(s, "height");
        asset.screen_origin_x = get_int(s, "originX");
        asset.screen_origin_y = get_int(s, "originY");
    }

    // resx
    std::unordered_map<std::string, std::string> resx_resources;
    if (!opts.resx_path.empty()) {
        std::string resx_str;
        if (read_file(opts.resx_path, resx_str)) {
            try {
                json resx = json::parse(resx_str);
                resx_resources = parse_resx_resources(resx);
            } catch (const std::exception& e) {
                result.report.warnings.push_back(std::string("resx parse error: ") + e.what());
            }
        } else {
            result.report.warnings.push_back("failed to read resx: " + opts.resx_path.string());
        }
    }

    // metadata
    if (d.contains("metadata") && d["metadata"].is_object()) {
        const json& md = d["metadata"];
        asset.variables = parse_variable_list(md, "variableList");
        asset.instant_variables = parse_variable_list(md, "instantVariableList");
        if (md.contains("customPartsOrder") && md["customPartsOrder"].is_array()) {
            for (const json& id : md["customPartsOrder"]) {
                if (id.is_string()) {
                    asset.parts_order.push_back(id.get<std::string>());
                }
            }
        }
    }

    // source: icons + textures
    if (d.contains("source") && d["source"].is_object()) {
        asset.icons = parse_icons(d["source"]);
        asset.textures = parse_textures(d["source"], resx_resources, result.report);
    }

    // object: part groups -> parts
    if (d.contains("object") && d["object"].is_object()) {
        LayerStats stats{};
        for (auto it = d["object"].begin(); it != d["object"].end(); ++it) {
            const std::string& group = it.key();
            const json& group_obj = it.value();
            if (!group_obj.is_object() || !group_obj.contains("motion") || !group_obj["motion"].is_object()) {
                continue;
            }
            for (auto pit = group_obj["motion"].begin(); pit != group_obj["motion"].end(); ++pit) {
                const std::string& part_name = pit.key();
                const json& part_obj = pit.value();
                if (!part_obj.is_object() || !part_obj.contains("layer") || !part_obj["layer"].is_array()) {
                    continue;
                }
                model::Part part;
                part.group = group;
                part.name = part_name;
                for (const json& layer : part_obj["layer"]) {
                    part.layers.push_back(parse_layer(layer, stats));
                }
                asset.parts.push_back(std::move(part));
            }
        }
        asset.total_layers = stats.layers;
        asset.total_frames = stats.frames;
        asset.bp_nonnull = stats.bp_nonnull;
        asset.cc_nonnull = stats.cc_nonnull;
    }

    result.report.parts_count = static_cast<int>(asset.parts.size());
    result.report.layers_count = asset.total_layers;
    result.report.frames_count = asset.total_frames;
    result.report.icons_count = static_cast<int>(asset.icons.size());
    result.report.textures_count = static_cast<int>(asset.textures.size());
    result.report.variables_count = static_cast<int>(asset.variables.size());

    result.ok = !asset.parts.empty();
    return result;
}

}  // namespace nekomote::asset_io
