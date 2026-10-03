#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Json = nlohmann::json;
using namespace nekomote;
struct Options
{
    std::filesystem::path asset, trace, output;
    Json variables = Json::object();
    int benchmark{0};
};
Json read(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());
    return Json::parse(input);
}
Options parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string flag = argv[i];
        if (i + 1 == argc)
            throw std::runtime_error("missing value for " + flag);
        const std::string value = argv[++i];
        if (flag == "--asset")
            o.asset = value;
        else if (flag == "--sample-trace")
            o.trace = value;
        else if (flag == "--output")
            o.output = value;
        else if (flag == "--variables")
            o.variables = Json::parse(value);
        else if (flag == "--benchmark")
            o.benchmark = std::stoi(value);
        else
            throw std::runtime_error("unknown option: " + flag);
    }
    if (o.asset.empty() || (!o.trace.empty() && o.output.empty()) || o.benchmark < 0 ||
        !o.variables.is_object())
        throw std::runtime_error("requires --asset; trace diagnostics require --output");
    return o;
}
void check(runtime::GeometryStatus status)
{
    if (!status)
        throw std::runtime_error(
            "geometry error=" + std::to_string(static_cast<int>(status.error)) +
            " node=" + std::to_string(status.node.value) +
            " parameter=" + std::to_string(status.parameter.value));
}
int run(const Options& o)
{
    auto loaded = asset_io::load_asset(o.asset);
    if (!loaded.ok)
        throw std::runtime_error(loaded.errors.front());
    auto compiled = runtime::compile_geometry_scene(loaded.asset);
    if (!compiled.scene)
        throw std::runtime_error(compiled.errors.front());
    // Native evaluation is independent of the source loader's lifetime.
    loaded = {};
    const auto& scene = *compiled.scene;
    runtime::GeometryInstance instance(scene);
    for (const auto& [name, value] : o.variables.items())
        check(instance.set_parameter(scene.find_variable(name), value.get<float>()));
    check(instance.evaluate());
    if (o.benchmark)
    {
        for (int i = 0; i < 120; ++i)
        {
            check(instance.evaluate());
            check(instance.build_frame());
        }
        std::vector<double> times(static_cast<std::size_t>(o.benchmark));
        for (auto& time : times)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto status = instance.evaluate();
            const auto built = instance.build_frame();
            time =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            check(status);
            check(built);
        }
        std::sort(times.begin(), times.end());
        const auto percentile = [&times](std::size_t p)
        { return times[(times.size() - 1) * p / 100]; };
        std::printf("evaluate+build_frame CPU ms P50=%.6f P95=%.6f P99=%.6f; nodes=%zu samples=%d "
                    "warmup=120\n",
                    percentile(50), percentile(95), percentile(99), scene.node_count(),
                    o.benchmark);
    }
    const auto frame_status = instance.build_frame();
    Json report{{"frame_error", static_cast<int>(frame_status.error)},
                {"frame_error_node", scene.node_label(frame_status.node)},
                {"frame_vertices", instance.frame().vertices.size()},
                {"frame_indices", instance.frame().indices.size()},
                {"certified", false},
                {"variables", o.variables},
                {"nodes", scene.node_count()},
                {"primitives", instance.primitives().size()},
                {"diagnostics", scene.diagnostics()}};
    if (!o.trace.empty())
    {
        auto metadata_file = o.trace;
        std::string path = metadata_file.string();
        const std::string suffix = ".trace.json";
        if (!path.ends_with(suffix))
            throw std::runtime_error("sample trace must end with .trace.json");
        path.resize(path.size() - suffix.size());
        const auto metadata = read(path + ".json");
        const auto draws = read(o.trace);
        Json comparisons = Json::array(), missing = Json::array();
        double largest = 0, largest_frame = 0;
        std::size_t total_unmatched = 0, total_unused = 0, primitive_index = 0;
        std::size_t total_missing_triangles = 0, total_unused_triangles = 0;
        const auto frame = instance.frame();
        for (const auto& p : instance.primitives())
        {
            const auto& rect = *p.rect;
            const auto& texture = scene.textures()[p.texture.value - 1];
            double maximum = 0, squared = 0;
            std::size_t samples = 0;
            Json mismatches = Json::array();
            std::vector<std::uint32_t> native_indices;
            std::set<std::array<std::uint32_t, 3>> native_triangles, reference_triangles;
            if (frame_status)
            {
                const auto& command = frame.commands[primitive_index];
                for (std::uint32_t j = 0; j < command.index_count; j += 3)
                {
                    std::array<std::uint32_t, 3> triangle{};
                    std::copy_n(frame.indices.begin() + command.first_index + j, 3,
                                triangle.begin());
                    std::sort(triangle.begin(), triangle.end());
                    native_triangles.insert(triangle);
                }
                native_indices.assign(frame.indices.begin() + command.first_index,
                                      frame.indices.begin() + command.first_index +
                                          command.index_count);
                std::sort(native_indices.begin(), native_indices.end());
                native_indices.erase(std::unique(native_indices.begin(), native_indices.end()),
                                     native_indices.end());
            }
            ++primitive_index;
            std::vector<bool> matched(native_indices.size());
            std::size_t unmatched = 0;
            double frame_maximum = 0;
            for (const auto& draw : draws)
            {
                if (!draw.contains("texture") || draw["texture"] != 2)
                    continue;
                std::vector<std::size_t> indices =
                    draw.at("indices").get<std::vector<std::size_t>>();
                std::sort(indices.begin(), indices.end());
                indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
                const auto& attributes = draw.at("attributes");
                std::vector<std::uint32_t> mapped(attributes.at("a_texCoord").at("data").size(),
                                                  std::numeric_limits<std::uint32_t>::max());
                for (const auto index : indices)
                {
                    const auto& uv = attributes.at("a_texCoord").at("data").at(index);
                    const float u = uv[0].get<float>();
                    const float v = static_cast<float>(texture.height) - uv[1].get<float>();
                    if (u < rect.left - 0.001f || u > rect.left + rect.width + 0.001f ||
                        v < rect.top - 0.001f || v > rect.top + rect.height + 0.001f)
                        continue;
                    const auto point = instance.sample_atlas_point(p.node, {u, v});
                    const double x =
                        static_cast<double>(point.x) * metadata.at("scale").get<double>() +
                        metadata.at("x").get<double>();
                    const double y =
                        static_cast<double>(point.y) * metadata.at("scale").get<double>() +
                        metadata.at("y").get<double>();
                    const auto& pos = attributes.at("a_pos").at("data").at(index);
                    const double error =
                        std::hypot(x - pos[0].get<double>(), y - pos[1].get<double>());
                    if (frame_status)
                    {
                        float uv_error = std::numeric_limits<float>::infinity();
                        std::size_t closest = 0;
                        for (std::size_t j = 0; j < native_indices.size(); ++j)
                        {
                            const auto& vertex = frame.vertices[native_indices[j]];
                            const auto distance =
                                std::hypot(vertex.u * static_cast<float>(texture.width) - u,
                                           vertex.v * static_cast<float>(texture.height) - v);
                            if (distance < uv_error)
                            {
                                uv_error = distance;
                                closest = j;
                            }
                        }
                        if (uv_error > 0.001f)
                            ++unmatched;
                        else
                        {
                            matched[closest] = true;
                            mapped[index] = native_indices[closest];
                            const auto& vertex = frame.vertices[native_indices[closest]];
                            const double vx =
                                static_cast<double>(vertex.x) * metadata.at("scale").get<double>() +
                                metadata.at("x").get<double>();
                            const double vy =
                                static_cast<double>(vertex.y) * metadata.at("scale").get<double>() +
                                metadata.at("y").get<double>();
                            frame_maximum =
                                std::max(frame_maximum, std::hypot(vx - pos[0].get<double>(),
                                                                   vy - pos[1].get<double>()));
                        }
                    }
                    maximum = std::max(maximum, error);
                    squared += error * error;
                    ++samples;
                    if (error > 0.001 && mismatches.size() < 3)
                        mismatches.push_back({{"uv", {u, v}},
                                              {"native", {x, y}},
                                              {"reference", pos},
                                              {"error", error}});
                }
                if (frame_status)
                {
                    const auto original_indices =
                        draw.at("indices").get<std::vector<std::size_t>>();
                    const int mode = draw.at("mode").get<int>();
                    if (mode != 4 && mode != 5)
                        throw std::runtime_error("unverified reference primitive topology");
                    for (std::size_t j = 0; j + 2 < original_indices.size(); j += mode == 4 ? 3 : 1)
                    {
                        std::array<std::uint32_t, 3> triangle{mapped[original_indices[j]],
                                                              mapped[original_indices[j + 1]],
                                                              mapped[original_indices[j + 2]]};
                        std::sort(triangle.begin(), triangle.end());
                        if (triangle[2] == std::numeric_limits<std::uint32_t>::max() ||
                            triangle[0] == triangle[1] || triangle[1] == triangle[2])
                            continue;
                        reference_triangles.insert(triangle);
                    }
                }
            }
            const auto missing_triangles =
                std::count_if(reference_triangles.begin(), reference_triangles.end(),
                              [&native_triangles](const auto& triangle)
                              { return !native_triangles.contains(triangle); });
            const auto unused_triangles =
                std::count_if(native_triangles.begin(), native_triangles.end(),
                              [&reference_triangles](const auto& triangle)
                              { return !reference_triangles.contains(triangle); });
            total_missing_triangles += static_cast<std::size_t>(missing_triangles);
            total_unused_triangles += static_cast<std::size_t>(unused_triangles);
            if (samples == 0)
                missing.push_back(p.label);
            else
                comparisons.push_back(
                    {{"source", p.source},
                     {"label", p.label},
                     {"samples", samples},
                     {"frame_vertices", native_indices.size()},
                     {"missing_reference_triangles", missing_triangles},
                     {"unused_native_triangles", unused_triangles},
                     {"unmatched_reference_vertices", unmatched},
                     {"unused_native_vertices", std::count(matched.begin(), matched.end(), false)},
                     {"frame_max_error_px", frame_maximum},
                     {"max_error", maximum},
                     {"rms_error", std::sqrt(squared / static_cast<double>(samples))},
                     {"mismatches", mismatches}});
            largest = std::max(largest, maximum);
            largest_frame = std::max(largest_frame, frame_maximum);
            total_unmatched += unmatched;
            total_unused +=
                static_cast<std::size_t>(std::count(matched.begin(), matched.end(), false));
        }
        report["comparisons"] = comparisons;
        report["missing_primitives"] = missing;
        report["max_error"] = largest;
        report["frame_max_error_px"] = largest_frame;
        report["unmatched_reference_vertices"] = total_unmatched;
        report["unused_native_vertices"] = total_unused;
        report["missing_reference_triangles"] = total_missing_triangles;
        report["unused_native_triangles"] = total_unused_triangles;
        std::printf("native nodes=%zu primitives=%zu max_error_px=%.9f missing=%zu\n",
                    scene.node_count(), instance.primitives().size(), largest, missing.size());
    }
    else
        std::printf("native nodes=%zu primitives=%zu complete composition pending P4\n",
                    scene.node_count(), instance.primitives().size());
    if (!o.output.empty())
    {
        std::ofstream output(o.output);
        if (!output)
            throw std::runtime_error("cannot create " + o.output.string());
        output << report.dump(2) << '\n';
        output.close();
        if (!output)
            throw std::runtime_error("failed to write " + o.output.string());
    }
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        return run(parse(argc, argv));
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "nekomote_pose: %s\n", e.what());
        return 1;
    }
}
