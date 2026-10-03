#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nekomote::model {

// Strongly typed model asset schema (immutable after import).
//
// This is the project's own representation of an imported E-mote model. It is
// produced by asset_io/importer from the source PSB/JSON and consumed by the
// runtime. The source field paths and raw E-mote values are not carried into
// the hot path; diagnostics live in the ImportReport instead.
//
// Field semantics and evidence: docs/p1-handoff.md, docs/conventions.md.
// Coordinate/UV/matrix conventions: see docs/conventions.md.

// Atlas sub-rectangle for an icon entry (source.tex.icon["NNNN"]).
struct IconRect {
    float left{0.0f};
    float top{0.0f};
    float width{0.0f};
    float height{0.0f};
    float origin_x{0.0f};
    float origin_y{0.0f};
    int attr{0};
    int zorder{0};
};

// Texture atlas definition (source.tex.texture + resx Resources).
struct TextureDef {
    std::string name;        // logical name, e.g. "tex"
    std::string path;        // path to PNG relative to resource root
    int width{0};            // atlas pixel width
    int height{0};           // atlas pixel height
    std::string format;      // e.g. "RGBA8"
};

// Mesh control points for one frame. bp is 16 control points x 2 coords,
// row-major (row 0..3, col 0..3), in normalized [0,1]^2 parameter space.
// has_bp=false means the frame uses the identity grid (col/3, row/3).
// cc (color control) is tracked for completeness; semantics unverified.
struct MeshData {
    std::optional<std::array<float, 32>> bp;
    std::optional<std::array<float, 32>> cc;
};

// Per-frame content of a layer. This is the rendering primitive.
struct FrameContent {
    std::array<float, 3> coord{{0.0f, 0.0f, 0.0f}};  // x, y, z(depth)
    std::string icon;        // raw icon string: "NNNN", "x:y:w:h", or name
    std::string src;         // texture name: "tex", "blank", part name, ...
    std::uint64_t mask{0};   // mask bitmask
    std::optional<float> opa;        // opacity; absent => unchanged
    std::optional<std::uint32_t> color;  // packed RGBA; absent => unchanged
    std::optional<int> bm;           // blend mode; absent => default
    MeshData mesh;
};

struct LayerFrame {
    int time{0};
    int type{0};
    FrameContent content;
};

// Parameter binding: maps a layer's frameList to a variable.
struct ParamBinding {
    std::string id;          // variable label, e.g. "face_talk"
    float range_begin{0.0f};
    float range_end{0.0f};
    int division{0};
    int discretization{0};
    bool enabled{true};
};

// A renderable layer. Children form the scene graph.
struct Layer {
    std::string label;
    int coordinate{0};
    int mesh_combine{0};     // 0=main, 1=helper(delta)
    int mesh_division{0};    // subdivision per edge
    int mesh_transform{0};   // 0=none, 1=bezier, 2=composite
    int parameterize{0};     // index into part's parameter table
    int stencil_type{0};
    std::uint64_t inherit_mask{0};
    std::vector<int> transform_order;
    std::vector<ParamBinding> parameter;
    std::vector<LayerFrame> frame_list;
    std::vector<Layer> children;
};

// A named part within a part group (e.g. face_parts/motion/口_基礎).
struct Part {
    std::string group;       // "face_parts", "body_parts", ...
    std::string name;        // part name
    std::vector<Layer> layers;
};

// Variable definition (metadata.variableList[]).
struct VariableDef {
    std::string label;
    std::vector<std::pair<int, std::string>> frames;  // (frame value, label)
};

// The immutable model asset.
struct ModelAsset {
    std::string spec;        // "ems"
    float version{0.0f};
    int screen_width{0};
    int screen_height{0};
    int screen_origin_x{0};
    int screen_origin_y{0};

    std::vector<VariableDef> variables;
    std::vector<VariableDef> instant_variables;

    std::unordered_map<std::string, IconRect> icons;   // "0003" -> rect
    std::vector<TextureDef> textures;

    std::vector<Part> parts;
    std::vector<std::string> parts_order;  // customPartsOrder (part ids)

    // Counts for diagnostics (filled by importer).
    int total_layers{0};
    int total_frames{0};
    int bp_nonnull{0};
    int cc_nonnull{0};
};

}  // namespace nekomote::model
