#include <nekomote/asset_io/import_emote.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace nekomote::asset_io
{
namespace
{
using Json = nlohmann::json;
using namespace model;

struct InputError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

void check(bool condition, const std::string& path, std::string_view message)
{
    if (!condition)
        throw InputError(path + ": " + std::string(message));
}

Json read_json(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    check(file.is_open(), path.string(), "cannot open file");
    Json value;
    file >> value;
    check(!file.bad(), path.string(), "read failed");
    check(value.is_object(), path.string(), "expected object");
    return value;
}

float number(const Json& j, const std::string& path)
{
    check(j.is_number(), path, "expected number");
    const double value = j.get<double>();
    check(std::isfinite(value) &&
              std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max()),
          path, "number is not finite float");
    return static_cast<float>(value);
}

template <typename T>
T integer(const Json& j, const std::string& path)
{
    check(j.is_number_integer(), path, "expected integer");
    if (j.is_number_unsigned())
        check(j.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()),
              path, "integer out of range");
    else
    {
        const auto value = j.get<std::int64_t>();
        if constexpr (std::is_unsigned_v<T>)
            check(value >= 0, path, "negative unsigned integer");
        else
            check(value >= std::numeric_limits<T>::min(), path, "integer out of range");
        if (value >= 0)
            check(static_cast<std::uint64_t>(value) <=
                      static_cast<std::uint64_t>(std::numeric_limits<T>::max()),
                  path, "integer out of range");
    }
    return j.get<T>();
}

std::string string(const Json& j, const std::string& path)
{
    check(j.is_string(), path, "expected string");
    return j.get<std::string>();
}

std::span<const Json> array(const Json& j, std::string_view path)
{
    check(j.is_array(), std::string(path), "expected array");
    const auto& values = j.get_ref<const Json::array_t&>();
    return {values.data(), values.size()};
}

template <typename T, typename Parse>
Field<T> field(const Json& object, const char* key, const std::string& path, Parse parse)
{
    Field<T> result;
    if (!object.contains(key))
        return result;
    if (object[key].is_null())
    {
        result.state = FieldState::Null;
        return result;
    }
    result.state = FieldState::Value;
    result.data = parse(object[key], path + "/" + key);
    return result;
}

int get_int(const Json& j, const char* key, const std::string& p, int fallback = 0)
{
    auto f = field<int>(j, key, p, integer<int>);
    return f.has_value() ? f.data : fallback;
}
float get_float(const Json& j, const char* key, const std::string& p, float fallback = 0)
{
    auto f = field<float>(j, key, p, number);
    return f.has_value() ? f.data : fallback;
}
std::string get_string(const Json& j, const char* key, const std::string& p)
{
    auto f = field<std::string>(j, key, p, string);
    return f.has_value() ? f.data : std::string{};
}

template <std::size_t N>
std::array<float, N> float_array(const Json& j, const std::string& p)
{
    array(j, p);
    check(j.size() == N, p, "wrong component count");
    std::array<float, N> result{};
    for (std::size_t i = 0; i < N; ++i)
        result[i] = number(j[i], p + "/" + std::to_string(i));
    return result;
}
std::vector<float> float_vector(const Json& j, const std::string& p)
{
    std::vector<float> result;
    for (const auto& v : array(j, p))
        result.push_back(number(v, p));
    return result;
}
std::vector<int> int_vector(const Json& j, const std::string& p)
{
    std::vector<int> result;
    for (const auto& v : array(j, p))
        result.push_back(integer<int>(v, p));
    return result;
}
std::vector<std::string> string_vector(const Json& j, const std::string& p)
{
    std::vector<std::string> result;
    for (const auto& v : array(j, p))
        result.push_back(string(v, p));
    return result;
}
void object(const Json& j, const std::string& p)
{
    check(j.is_object(), p, "expected object");
}

// Unknown fields are explicitly reported and preserved in the offline archive.
// This is structural import, not permission to play unsupported semantics.
void unknown_fields(const Json& j, const std::string& path,
                    std::initializer_list<std::string_view> known, ImportReport& report)
{
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        if (std::find(known.begin(), known.end(), it.key()) == known.end() && !it->is_null())
            report.unsupported_fields.push_back(path + "/" + it.key());
    }
}

MeshData mesh(const Json& j, const std::string& p)
{
    object(j, p);
    MeshData result;
    result.bp = field<std::array<float, 32>>(j, "bp", p, float_array<32>);
    result.cc = field<std::vector<float>>(j, "cc", p, float_vector);
    return result;
}
MotionContent motion_content(const Json& j, const std::string& p)
{
    object(j, p);
    MotionContent result;
    result.mask = field<std::uint64_t>(j, "mask", p, integer<std::uint64_t>);
    result.time_offset = field<float>(j, "timeOffset", p, number);
    return result;
}
FrameContent content(const Json& j, const std::string& p, ImportReport& report)
{
    object(j, p);
    FrameContent result;
    result.coord = field<std::array<float, 3>>(j, "coord", p, float_array<3>);
    result.icon = field<std::string>(j, "icon", p, string);
    result.src = field<std::string>(j, "src", p, string);
    result.mask = field<std::uint64_t>(j, "mask", p, integer<std::uint64_t>);
    result.color = field<std::uint32_t>(
        j, "color", p,
        [](const Json& value, const std::string& path)
        {
            if (value.is_number_integer() && !value.is_number_unsigned() &&
                value.get<std::int64_t>() < 0)
                return static_cast<std::uint32_t>(integer<std::int32_t>(value, path));
            return integer<std::uint32_t>(value, path);
        });
    result.bm = field<int>(j, "bm", p, integer<int>);
    result.flip = field<int>(j, "flip", p, integer<int>);
    result.opa = field<float>(j, "opa", p, number);
    result.angle = field<float>(j, "angle", p, number);
    result.zx = field<float>(j, "zx", p, number);
    result.zy = field<float>(j, "zy", p, number);
    result.ox = field<float>(j, "ox", p, number);
    result.oy = field<float>(j, "oy", p, number);
    result.mesh = field<MeshData>(j, "mesh", p, mesh);
    result.motion = field<MotionContent>(j, "motion", p, motion_content);
    unknown_fields(j, p,
                   {"coord", "icon", "src", "mask", "color", "bm", "opa", "angle", "zx", "zy", "ox",
                    "oy", "flip", "mesh", "motion"},
                   report);
    return result;
}
std::vector<ParamBinding> parameters(const Json& j, const std::string& p)
{
    std::vector<ParamBinding> result;
    if (!j.contains("parameter") || j["parameter"].is_null())
        return result;
    for (const auto& v : array(j["parameter"], p + "/parameter"))
    {
        object(v, p);
        ParamBinding b;
        b.id = get_string(v, "id", p);
        b.range_begin = get_float(v, "rangeBegin", p);
        b.range_end = get_float(v, "rangeEnd", p);
        b.division = get_int(v, "division", p);
        b.discretization = get_int(v, "discretization", p);
        b.enabled = get_int(v, "enabled", p, 1) != 0;
        check(!b.id.empty(), p, "empty parameter id");
        check(b.division >= 0 && b.range_begin <= b.range_end, p,
              "invalid parameter range/division");
        result.push_back(std::move(b));
    }
    return result;
}
Layer layer(const Json& j, const std::string& p, ModelAsset& asset, ImportReport& report, int depth)
{
    check(depth < 256, p, "layer depth exceeds 255");
    object(j, p);
    Layer result;
    result.label = get_string(j, "label", p);
    result.type = get_int(j, "type", p);
    result.coordinate = get_int(j, "coordinate", p);
    result.mesh_combine = get_int(j, "meshCombine", p);
    result.mesh_division = get_int(j, "meshDivision", p);
    result.mesh_transform = get_int(j, "meshTransform", p);
    result.stencil_type = get_int(j, "stencilType", p);
    result.original_index = get_int(j, "originalIndex", p);
    result.mesh_sync_child_mask = get_int(j, "meshSyncChildMask", p);
    result.export_self = get_int(j, "exportSelf", p, 1);
    result.join_target = get_int(j, "joinTarget", p, 1);
    result.ground_correction = get_int(j, "groundCorrection", p);
    result.motion_independent_layer_inherit = get_int(j, "motionIndependentLayerInherit", p);
    result.obj_tri_priority = get_int(j, "objTriPriority", p);
    result.parameterize = field<int>(j, "parameterize", p, integer<int>);
    result.shape = field<int>(j, "shape", p, integer<int>);
    auto inherit = field<std::uint64_t>(j, "inheritMask", p, integer<std::uint64_t>);
    if (inherit.has_value())
        result.inherit_mask = inherit.data;
    if (j.contains("transformOrder"))
        result.transform_order = int_vector(j["transformOrder"], p + "/transformOrder");
    if (j.contains("stencilCompositeMaskLayerList"))
        result.stencil_composite_mask_layers =
            string_vector(j["stencilCompositeMaskLayerList"], p + "/stencilCompositeMaskLayerList");
    result.parameter = parameters(j, p);
    check(result.mesh_division >= 0 && result.mesh_division <= 1024, p + "/meshDivision",
          "invalid subdivision");
    if (j.contains("frameList"))
    {
        std::size_t index = 0;
        for (const auto& f : array(j["frameList"], p + "/frameList"))
        {
            const auto fp = p + "/frameList/" + std::to_string(index++);
            object(f, fp);
            LayerFrame frame;
            frame.time = get_float(f, "time", fp);
            frame.type = get_int(f, "type", fp);
            frame.content = field<FrameContent>(f, "content", fp,
                                                [&report](const Json& c, const std::string& cp)
                                                { return content(c, cp, report); });
            check(result.frame_list.empty() || frame.time >= result.frame_list.back().time,
                  fp + "/time", "frames out of order");
            if (frame.content.has_value() && frame.content.data.mesh.has_value())
            {
                asset.bp_nonnull += frame.content.data.mesh.data.bp.has_value() ? 1 : 0;
                asset.cc_nonnull += frame.content.data.mesh.data.cc.has_value() ? 1 : 0;
            }
            result.frame_list.push_back(std::move(frame));
            ++asset.total_frames;
        }
    }
    if (j.contains("children"))
    {
        std::size_t i = 0;
        for (const auto& c : array(j["children"], p + "/children"))
            result.children.push_back(
                layer(c, p + "/children/" + std::to_string(i++), asset, report, depth + 1));
    }
    unknown_fields(j, p,
                   {"label",
                    "type",
                    "coordinate",
                    "meshCombine",
                    "meshDivision",
                    "meshTransform",
                    "stencilType",
                    "originalIndex",
                    "meshSyncChildMask",
                    "exportSelf",
                    "joinTarget",
                    "groundCorrection",
                    "motionIndependentLayerInherit",
                    "objTriPriority",
                    "parameterize",
                    "shape",
                    "inheritMask",
                    "transformOrder",
                    "stencilCompositeMaskLayerList",
                    "parameter",
                    "frameList",
                    "children",
                    "metadata",
                    "editVisible"},
                   report);
    ++asset.total_layers;
    return result;
}
std::vector<VariableDef> variables(const Json& md, const char* key)
{
    std::vector<VariableDef> result;
    if (!md.contains(key))
        return result;
    for (const auto& v : array(md[key], std::string("/metadata/") + key))
    {
        VariableDef def;
        def.label = get_string(v, "label", "/metadata/" + std::string(key));
        for (const auto& f :
             array(v.at("frameList"), "/metadata/" + std::string(key) + "/frameList"))
            def.frames.emplace_back(get_int(f, "frame", "/metadata/" + std::string(key)),
                                    get_string(f, "label", "/metadata/" + std::string(key)));
        result.push_back(std::move(def));
    }
    return result;
}
void validate_references(const ModelAsset& asset)
{
    auto walk = [&](auto&& self, const Layer& l, const Part& part) -> void
    {
        const std::string p =
            "/object/" + part.group + "/motion/" + part.name + "/layer/" + l.label;
        const auto& bindings = l.parameter.empty() ? part.parameter : l.parameter;
        if (l.parameterize.has_value())
            check(l.parameterize.data >= 0 &&
                      static_cast<std::size_t>(l.parameterize.data) < bindings.size(),
                  p + "/parameterize", "parameter index out of range");
        for (const auto& f : l.frame_list)
        {
            if (!f.content.has_value())
                continue;
            const auto& c = f.content.data;
            if (!c.src.has_value() || !c.icon.has_value() || c.src.data == "blank")
                continue;
            const auto tex =
                std::find_if(asset.textures.begin(), asset.textures.end(),
                             [&c](const TextureDef& t) { return t.name == c.src.data; });
            if (tex != asset.textures.end())
                check(asset.icons.contains(c.src.data + "/" + c.icon.data),
                      p + "/frameList/content/icon", "unresolved atlas icon");
            else
                check(std::any_of(
                          asset.parts.begin(), asset.parts.end(), [&c](const Part& target)
                          { return target.group == c.src.data && target.name == c.icon.data; }),
                      p + "/frameList/content/src", "unresolved motion reference");
        }
        for (const auto& child : l.children)
            self(self, child, part);
    };
    for (const auto& part : asset.parts)
        for (const auto& l : part.layers)
            walk(walk, l, part);
    if (!asset.base_group.empty())
        check(std::any_of(asset.parts.begin(), asset.parts.end(), [&asset](const Part& p)
                          { return p.group == asset.base_group && p.name == asset.base_motion; }),
              "/metadata/base", "unresolved base motion");
}

ImportResult parse(const ImportOptions& opts)
{
    ImportResult result;
    const Json d = read_json(opts.json_path), resx = read_json(opts.resx_path);
    result.source_archive = d.dump();
    auto& a = result.asset;
    a.spec = get_string(d, "spec", "");
    a.version = get_float(d, "version", "");
    check(a.spec == "ems", "/spec", "unsupported source specification");
    check(a.version >= 3 && a.version < 4, "/version", "unsupported source version");
    const auto& screen = d.at("screenSize");
    object(screen, "/screenSize");
    a.screen_width = get_int(screen, "width", "/screenSize");
    a.screen_height = get_int(screen, "height", "/screenSize");
    a.screen_origin_x = get_int(screen, "originX", "/screenSize");
    a.screen_origin_y = get_int(screen, "originY", "/screenSize");
    check(a.screen_width > 0 && a.screen_height > 0, "/screenSize", "invalid dimensions");
    const auto& source = d.at("source");
    object(source, "/source");
    const auto& resources = resx.at("Resources");
    object(resources, "resx/Resources");
    for (auto it = source.begin(); it != source.end(); ++it)
    {
        const auto p = "/source/" + it.key();
        const auto& s = it.value();
        object(s, p);
        const auto& t = s.at("texture");
        TextureDef tex;
        tex.name = it.key();
        tex.width = get_int(t, "width", p + "/texture");
        tex.height = get_int(t, "height", p + "/texture");
        tex.format = get_string(t, "type", p + "/texture");
        const auto pixel = field<std::string>(t, "pixel", p + "/texture", string);
        if (pixel.has_value())
        {
            check(pixel.data.starts_with("#resource#"), p + "/texture/pixel",
                  "expected resource reference");
            const auto key = pixel.data.substr(10);
            check(resources.contains(key), p + "/texture/pixel", "missing resx resource " + key);
            tex.path = string(resources.at(key), "resx/Resources/" + key);
        }
        else
        {
            const auto external = opts.external_textures.find(tex.name);
            check(external != opts.external_textures.end(), p + "/texture/pixel",
                  "external texture requires explicit --texture name=relative-path mapping");
            tex.path = external->second;
        }
        const std::filesystem::path relative{tex.path};
        check(!relative.empty() && !relative.is_absolute(), p + "/texture/pixel",
              "expected relative resource path");
        for (const auto& segment : relative)
            check(segment != "..", p + "/texture/pixel", "path traversal");
        check(tex.width > 0 && tex.height > 0 && tex.width <= 32768 && tex.height <= 32768,
              p + "/texture", "invalid atlas size");
        if (!opts.resource_root.empty())
            check(std::filesystem::is_regular_file(opts.resource_root / relative),
                  p + "/texture/pixel", "texture file missing: " + relative.string());
        if (result.report.texture_path.empty())
            result.report.texture_path = tex.path;
        for (auto icon = s.at("icon").begin(); icon != s.at("icon").end(); ++icon)
        {
            const auto ip = p + "/icon/" + icon.key();
            const auto& v = icon.value();
            object(v, ip);
            IconRect rect;
            rect.texture = tex.name;
            rect.left = get_float(v, "left", ip);
            rect.top = get_float(v, "top", ip);
            rect.width = get_float(v, "width", ip);
            rect.height = get_float(v, "height", ip);
            rect.origin_x = get_float(v, "originX", ip);
            rect.origin_y = get_float(v, "originY", ip);
            rect.attr = get_int(v, "attr", ip);
            if (v.contains("metadata") && v["metadata"].is_object())
                rect.zorder = get_int(v["metadata"], "zorder", ip + "/metadata");
            check(rect.left >= 0 && rect.top >= 0 && rect.width > 0 && rect.height > 0 &&
                      rect.left + rect.width <= static_cast<float>(tex.width) &&
                      rect.top + rect.height <= static_cast<float>(tex.height),
                  ip, "icon outside atlas");
            a.icons.emplace(tex.name + "/" + icon.key(), rect);
        }
        a.textures.push_back(std::move(tex));
    }
    const auto& objects = d.at("object");
    object(objects, "/object");
    for (auto group = objects.begin(); group != objects.end(); ++group)
    {
        const auto gp = "/object/" + group.key();
        object(group.value(), gp);
        const auto& motions = group.value().at("motion");
        object(motions, gp + "/motion");
        for (auto it = motions.begin(); it != motions.end(); ++it)
        {
            const auto p = gp + "/motion/" + it.key();
            const auto& m = it.value();
            object(m, p);
            Part part;
            part.group = group.key();
            part.name = it.key();
            part.type = get_int(m, "type", p);
            part.last_time = get_float(m, "lastTime", p);
            part.loop_time = get_float(m, "loopTime", p, -1);
            part.parameterize = field<int>(m, "parameterize", p, integer<int>);
            part.parameter = parameters(m, p);
            if (m.contains("layerIndexMap"))
                for (auto idx = m["layerIndexMap"].begin(); idx != m["layerIndexMap"].end(); ++idx)
                    part.layer_index_map.emplace(
                        idx.key(), integer<int>(idx.value(), p + "/layerIndexMap/" + idx.key()));
            if (m.contains("priority"))
                for (const auto& f : array(m["priority"], p + "/priority"))
                {
                    PriorityFrame pf;
                    pf.time = get_float(f, "time", p + "/priority");
                    pf.type = get_int(f, "type", p + "/priority");
                    pf.content = field<std::vector<int>>(f, "content", p + "/priority", int_vector);
                    part.priority.push_back(std::move(pf));
                }
            std::size_t i = 0;
            for (const auto& l : array(m.at("layer"), p + "/layer"))
                part.layers.push_back(
                    layer(l, p + "/layer/" + std::to_string(i++), a, result.report, 0));
            unknown_fields(m, p,
                           {"type", "lastTime", "loopTime", "parameterize", "parameter",
                            "layerIndexMap", "priority", "layer", "bounds", "metadata",
                            "referenceModelFileList", "referenceProjectFileList", "tag",
                            "variable"},
                           result.report);
            if (m.contains("variable") && !m["variable"].empty())
                result.report.unsupported_fields.push_back(p + "/variable");
            a.parts.push_back(std::move(part));
        }
    }
    const auto& md = d.at("metadata");
    object(md, "/metadata");
    a.variables = variables(md, "variableList");
    if (md.contains("instantVariableList"))
        a.instant_variables =
            string_vector(md["instantVariableList"], "/metadata/instantVariableList");
    if (md.contains("base"))
    {
        a.base_group = get_string(md["base"], "chara", "/metadata/base");
        a.base_motion = get_string(md["base"], "motion", "/metadata/base");
    }
    if (md.contains("customPartsOrder"))
        a.parts_order = string_vector(md["customPartsOrder"], "/metadata/customPartsOrder");
    if (md.contains("clampControl"))
    {
        std::size_t index = 0;
        for (const auto& c : array(md["clampControl"], "/metadata/clampControl"))
        {
            const auto p = "/metadata/clampControl/" + std::to_string(index++);
            object(c, p);
            PairedClamp clamp;
            clamp.label = get_string(c, "label", p);
            clamp.variable_x = get_string(c, "var_lr", p);
            clamp.variable_y = get_string(c, "var_ud", p);
            clamp.range_begin = get_float(c, "min", p);
            clamp.range_end = get_float(c, "max", p);
            clamp.type = get_int(c, "type", p);
            const auto enabled = get_int(c, "enabled", p, 1);
            check(enabled == 0 || enabled == 1, p + "/enabled", "expected 0 or 1");
            clamp.enabled = enabled == 1;
            check(!clamp.variable_x.empty() && !clamp.variable_y.empty() &&
                      clamp.variable_x != clamp.variable_y,
                  p, "clamp requires two distinct variables");
            check(clamp.range_begin < clamp.range_end, p, "invalid clamp range");
            a.paired_clamps.push_back(std::move(clamp));
            unknown_fields(
                c, p, {"label", "var_lr", "var_ud", "min", "max", "type", "enabled", "comment"},
                result.report);
        }
    }
    if (md.contains("timelineControl"))
        for (const auto& t : array(md["timelineControl"], "/metadata/timelineControl"))
        {
            Timeline timeline;
            const auto p =
                "/metadata/timelineControl/" + get_string(t, "label", "/metadata/timelineControl");
            timeline.label = get_string(t, "label", p);
            timeline.diff = get_int(t, "diff", p);
            timeline.last_time = get_float(t, "lastTime", p, -1);
            timeline.loop_begin = get_float(t, "loopBegin", p, -1);
            timeline.loop_end = get_float(t, "loopEnd", p, -1);
            for (const auto& track : array(t.at("variableList"), p + "/variableList"))
            {
                VariableTrack vt;
                vt.label = get_string(track, "label", p);
                for (const auto& f :
                     array(track.at("frameList"), p + "/" + vt.label + "/frameList"))
                {
                    VariableKey vk;
                    vk.time = get_float(f, "time", p);
                    vk.type = get_int(f, "type", p);
                    if (f.contains("content") && !f["content"].is_null())
                    {
                        vk.value = field<float>(f["content"], "value", p, number);
                        vk.easing = field<float>(f["content"], "easing", p, number);
                    }
                    else if (f.contains("content"))
                    {
                        vk.value.state = FieldState::Null;
                        vk.easing.state = FieldState::Null;
                    }
                    vt.keys.push_back(std::move(vk));
                }
                timeline.tracks.push_back(std::move(vt));
            }
            a.timelines.push_back(std::move(timeline));
        }
    for (const auto& [collection, kind] :
         {std::pair{"transitionControl", VariableControlKind::Linear},
          {"immediateControl", VariableControlKind::Immediate},
          {"eyebrowControl", VariableControlKind::Expression},
          {"eyeControl", VariableControlKind::Eye},
          {"mouthControl", VariableControlKind::Mouth},
          {"selectorControl", VariableControlKind::Selector}})
    {
        if (!md.contains(collection))
            continue;
        std::size_t index = 0;
        for (const auto& c : array(md[collection], std::string("/metadata/") + collection))
        {
            const auto p = std::string("/metadata/") + collection + "/" + std::to_string(index++);
            object(c, p);
            VariableControl control;
            control.kind = kind;
            control.variable = get_string(c, "label", p);
            const auto flag = [&](const char* key, int fallback)
            {
                const auto value = get_int(c, key, p, fallback);
                check(value == 0 || value == 1, p + "/" + key, "expected 0 or 1");
                return value == 1;
            };
            control.enabled = flag("enabled", 1);
            control.blink_enabled = flag("blinkEnabled", 0);
            control.begin_frame = get_float(c, "beginFrame", p);
            control.end_frame = get_float(c, "endFrame", p);
            control.blink_frames = get_float(c, "blinkFrameCount", p);
            control.blink_interval_min = get_float(c, "blinkIntervalMin", p);
            control.blink_interval_max = get_float(c, "blinkIntervalMax", p);
            control.talk_variable = get_string(c, "talkLabel", p);
            if (c.contains("edge"))
                for (const auto& edge : array(c["edge"], p + "/edge"))
                    control.edges.push_back(float_array<2>(edge, p + "/edge"));
            if (c.contains("node"))
                for (const auto& node : array(c["node"], p + "/node"))
                    control.junctions.push_back(float_vector(node, p + "/node"));
            if (c.contains("optionList"))
                for (const auto& option : array(c["optionList"], p + "/optionList"))
                {
                    object(option, p + "/optionList");
                    const auto name = get_string(option, "label", p + "/optionList");
                    check(!name.empty() && option.contains("onValue") &&
                              option.contains("offValue"),
                          p + "/optionList", "selector output requires label/onValue/offValue");
                    control.options.push_back(
                        {name, number(option["onValue"], p + "/optionList/onValue"),
                         number(option["offValue"], p + "/optionList/offValue")});
                    unknown_fields(option, p + "/optionList", {"label", "onValue", "offValue"},
                                   result.report);
                }
            check(!control.variable.empty(), p, "empty controller variable");
            for (const auto& edge : control.edges)
                check(edge[0] <= edge[1], p + "/edge", "invalid edge range");
            check(control.blink_frames >= 0 && control.blink_interval_min >= 0 &&
                      control.blink_interval_max >= control.blink_interval_min,
                  p, "invalid blink timing");
            a.variable_controls.push_back(std::move(control));
            unknown_fields(c, p,
                           {"label", "enabled", "beginFrame", "endFrame", "blinkEnabled",
                            "blinkFrameCount", "blinkIntervalMin", "blinkIntervalMax", "talkLabel",
                            "edge", "node", "optionList", "parameter", "comment", "comemnt"},
                           result.report);
        }
    }
    unknown_fields(md, "/metadata",
                   {"variableList", "instantVariableList", "customPartsOrder", "timelineControl",
                    "base", "clampControl", "transitionControl", "immediateControl",
                    "eyebrowControl", "eyeControl", "mouthControl", "selectorControl"},
                   result.report);
    unknown_fields(d, "",
                   {"spec", "version", "screenSize", "source", "object", "metadata", "id", "label"},
                   result.report);
    validate_references(a);
    check(!a.parts.empty() && !a.textures.empty(), "/", "model has no motions or textures");
    auto& r = result.report;
    r.parts_count = static_cast<int>(a.parts.size());
    r.layers_count = a.total_layers;
    r.frames_count = a.total_frames;
    r.icons_count = static_cast<int>(a.icons.size());
    r.textures_count = static_cast<int>(a.textures.size());
    r.variables_count = static_cast<int>(a.variables.size());
    if (!r.unsupported_fields.empty())
        r.warnings.push_back("structural import only: unsupported fields archived; complete "
                             "playback is not certified");
    result.ok = true;
    return result;
}
} // namespace

ImportResult import_emote(const ImportOptions& opts)
{
    try
    {
        return parse(opts);
    }
    catch (const InputError& e)
    {
        ImportResult r;
        r.report.errors.push_back(opts.json_path.string() + ": " + e.what());
        return r;
    }
    catch (const Json::exception& e)
    {
        ImportResult r;
        r.report.errors.push_back(opts.json_path.string() +
                                  ": invalid model/resx JSON: " + e.what());
        return r;
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        ImportResult r;
        r.report.errors.push_back(e.what());
        return r;
    }
}
} // namespace nekomote::asset_io
