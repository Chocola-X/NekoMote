#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/runtime/geometry_scene.hpp>

#include <cstdio>
#include <filesystem>
int main(int argc, char** argv)
{
    using namespace nekomote;
    int total = 0, failed = 0, protected_frames = 0, multiply_frames = 0, masked = 0, eligible = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator(argv[1]))
    {
        if (entry.path().filename() != "model.nkm")
            continue;
        ++total;
        auto a = asset_io::load_asset(entry.path());
        if (!a.ok)
        {
            ++failed;
            continue;
        }
        auto c = runtime::compile_geometry_scene(a.asset);
        if (!c.scene)
        {
            ++failed;
            continue;
        }
        runtime::GeometryInstance i(*c.scene);
        if (!i.evaluate() || !i.build_frame())
        {
            ++failed;
            continue;
        }
        auto f = i.frame();
        bool p = false, m = false;
        for (auto& d : f.commands)
        {
            p |= d.protection != ColorProtection::None;
            m |= d.blend == BlendMode::Multiply;
        }
        protected_frames += p;
        multiply_frames += m;
        masked += !f.masks.empty();
        eligible += !p && !m;
    }
    std::printf("initial geometry inventory total=%d failed=%d protected=%d multiply=%d masked=%d "
                "direct_candidate=%d\n",
                total, failed, protected_frames, multiply_frames, masked, eligible);
}
