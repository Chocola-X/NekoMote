#include "desktop_gaze.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

namespace nekomote::layer_shell
{
namespace
{
template <class T>
T* checked(T* object)
{
    if (!object)
        throw std::runtime_error("cannot allocate desktop gaze Wayland object");
    return object;
}

template <class T>
T* bind_global(wl_registry* registry, uint32_t name, const wl_interface& interface,
               uint32_t version)
{
    return checked(static_cast<T*>(wl_registry_bind(registry, name, &interface, version)));
}

using detail::GazeLayout;
struct Mode
{
    int width = 0, height = 0, transform = -1;
};

struct Output
{
    wl_output* object = nullptr;
    zxdg_output_v1* xdg = nullptr;
    ext_image_copy_capture_cursor_session_v1* session = nullptr;
    GazeLayout layout, pending_layout;
    Mode mode, pending_mode;
    uint64_t* clock = nullptr;
    bool entered = false;
    uint64_t position_order = 0;
    int cursor_x = 0, cursor_y = 0;

    void stop()
    {
        if (session)
            ext_image_copy_capture_cursor_session_v1_destroy(std::exchange(session, nullptr));
        entered = false;
        position_order = 0;
    }
    void drop_layout()
    {
        if (xdg)
            zxdg_output_v1_destroy(std::exchange(xdg, nullptr));
        pending_layout = layout = {};
    }
    ~Output()
    {
        stop();
        drop_layout();
    }
};

const zxdg_output_v1_listener layout_listener{
    [](void* data, zxdg_output_v1*, int32_t x, int32_t y)
    {
        auto& layout = static_cast<Output*>(data)->pending_layout;
        layout.x = x;
        layout.y = y;
        layout.positioned = true;
    },
    [](void* data, zxdg_output_v1*, int32_t width, int32_t height)
    {
        auto& layout = static_cast<Output*>(data)->pending_layout;
        layout.width = width;
        layout.height = height;
    },
    [](void* data, zxdg_output_v1* xdg)
    {
        auto& out = *static_cast<Output*>(data);
        if (zxdg_output_v1_get_version(xdg) < 3)
            out.layout = out.pending_layout;
    },
    [](void*, zxdg_output_v1*, const char*) {},
    [](void*, zxdg_output_v1*, const char*) {},
};

const ext_image_copy_capture_cursor_session_v1_listener cursor_listener{
    [](void* data, ext_image_copy_capture_cursor_session_v1*)
    {
        auto& out = *static_cast<Output*>(data);
        out.entered = true;
        out.position_order = 0;
    },
    [](void* data, ext_image_copy_capture_cursor_session_v1*)
    {
        auto& out = *static_cast<Output*>(data);
        out.entered = false;
        out.position_order = 0;
    },
    [](void* data, ext_image_copy_capture_cursor_session_v1*, int32_t x, int32_t y)
    {
        auto& out = *static_cast<Output*>(data);
        if (out.entered)
        {
            out.cursor_x = x;
            out.cursor_y = y;
            out.position_order = ++*out.clock;
        }
    },
    // position already reports the hotspot, not the origin of the cursor image.
    [](void*, ext_image_copy_capture_cursor_session_v1*, int32_t, int32_t) {},
};
} // namespace

struct DesktopGaze::Impl
{
    wl_registry* registry = nullptr;
    wl_pointer* pointer = nullptr;
    ext_output_image_capture_source_manager_v1* source_manager = nullptr;
    ext_image_copy_capture_manager_v1* copy_manager = nullptr;
    zxdg_output_manager_v1* layout_manager = nullptr;
    uint32_t source_name = 0, copy_name = 0, layout_name = 0;
    uint64_t clock = 0;
    std::map<uint32_t, Output> outputs; // stable listener userdata

    Output* find(wl_output* object)
    {
        for (auto& entry : outputs)
            if (entry.second.object == object)
                return &entry.second;
        return nullptr;
    }
    void update(Output& out)
    {
        if (!source_manager || !copy_manager || !layout_manager)
            return;
        if (!out.xdg)
        {
            auto* manager = layout_manager;
            // wl_output v1 cannot delimit a v3 xdg-output transaction with done.
            if (wl_output_get_version(out.object) == 1 &&
                zxdg_output_manager_v1_get_version(manager) >= 3)
                manager = bind_global<zxdg_output_manager_v1>(registry, layout_name,
                                                              zxdg_output_manager_v1_interface, 2);
            out.xdg = zxdg_output_manager_v1_get_xdg_output(manager, out.object);
            if (manager != layout_manager)
                zxdg_output_manager_v1_destroy(manager);
            if (zxdg_output_v1_add_listener(checked(out.xdg), &layout_listener, &out) < 0)
                throw std::runtime_error("cannot listen to desktop gaze layout");
        }
        if (pointer && !out.session)
        {
            auto* source = checked(ext_output_image_capture_source_manager_v1_create_source(
                source_manager, out.object));
            out.session = ext_image_copy_capture_manager_v1_create_pointer_cursor_session(
                copy_manager, source, pointer);
            ext_image_capture_source_v1_destroy(source);
            if (ext_image_copy_capture_cursor_session_v1_add_listener(checked(out.session),
                                                                      &cursor_listener, &out) < 0)
                throw std::runtime_error("cannot listen to desktop gaze cursor");
        }
    }
    void update_all()
    {
        for (auto& entry : outputs)
            update(entry.second);
    }
    void stop_all()
    {
        for (auto& entry : outputs)
            entry.second.stop();
    }
};

DesktopGaze::DesktopGaze() = default;
DesktopGaze::~DesktopGaze()
{
    reset();
}

void DesktopGaze::global(wl_registry* registry, uint32_t name, const char* interface,
                         uint32_t version)
{
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    auto& s = *impl_;
    s.registry = registry;
    if (!s.source_manager &&
        std::strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0)
    {
        s.source_manager = bind_global<ext_output_image_capture_source_manager_v1>(
            registry, name, ext_output_image_capture_source_manager_v1_interface, 1);
        s.source_name = name;
    }
    else if (!s.copy_manager &&
             std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0)
    {
        s.copy_manager = bind_global<ext_image_copy_capture_manager_v1>(
            registry, name, ext_image_copy_capture_manager_v1_interface, 1);
        s.copy_name = name;
    }
    else if (!s.layout_manager &&
             std::strcmp(interface, zxdg_output_manager_v1_interface.name) == 0)
    {
        s.layout_manager = bind_global<zxdg_output_manager_v1>(
            registry, name, zxdg_output_manager_v1_interface, std::min(version, 3U));
        s.layout_name = name;
    }
    else
        return;
    s.update_all();
}

void DesktopGaze::remove_global(uint32_t name)
{
    if (!impl_)
        return;
    auto& s = *impl_;
    remove_output(name);
    if (name == s.source_name && s.source_manager)
    {
        s.stop_all();
        ext_output_image_capture_source_manager_v1_destroy(
            std::exchange(s.source_manager, nullptr));
    }
    if (name == s.copy_name && s.copy_manager)
    {
        s.stop_all();
        ext_image_copy_capture_manager_v1_destroy(std::exchange(s.copy_manager, nullptr));
    }
    if (name == s.layout_name && s.layout_manager)
    {
        s.stop_all();
        for (auto& entry : s.outputs)
            entry.second.drop_layout();
        zxdg_output_manager_v1_destroy(std::exchange(s.layout_manager, nullptr));
    }
}

void DesktopGaze::add_output(uint32_t name, wl_output* object)
{
    if (!object)
        return;
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    auto& outputs = impl_->outputs;
    auto iter = outputs.find(name);
    if (iter != outputs.end() && iter->second.object != object)
        outputs.erase(iter);
    auto& out = outputs[name];
    out.object = object;
    out.clock = &impl_->clock;
    impl_->update(out);
}
void DesktopGaze::remove_output(uint32_t name)
{
    if (impl_)
        impl_->outputs.erase(name);
}
void DesktopGaze::set_pointer(wl_pointer* pointer)
{
    if (!impl_ && pointer)
        impl_ = std::make_unique<Impl>();
    if (!impl_ || impl_->pointer == pointer)
        return;
    impl_->stop_all();
    impl_->pointer = pointer;
    impl_->update_all();
}
void DesktopGaze::set_output_mode(wl_output* object, int width, int height)
{
    if (auto* out = impl_ ? impl_->find(object) : nullptr)
    {
        out->pending_mode.width = width;
        out->pending_mode.height = height;
        if (wl_output_get_version(object) == 1)
            out->mode = out->pending_mode;
    }
}
void DesktopGaze::set_output_transform(wl_output* object, int transform)
{
    if (auto* out = impl_ ? impl_->find(object) : nullptr)
    {
        out->pending_mode.transform = transform;
        if (wl_output_get_version(object) == 1)
            out->mode = out->pending_mode;
    }
}
void DesktopGaze::output_done(wl_output* object)
{
    if (auto* out = impl_ ? impl_->find(object) : nullptr)
    {
        out->mode = out->pending_mode;
        if (out->xdg && zxdg_output_v1_get_version(out->xdg) >= 3)
            out->layout = out->pending_layout;
    }
}
bool DesktopGaze::protocols_available() const
{
    return impl_ && impl_->source_manager && impl_->copy_manager && impl_->layout_manager;
}
bool DesktopGaze::available() const
{
    return protocols_available() && impl_->pointer;
}

std::optional<std::pair<double, double>> DesktopGaze::sample(wl_output* target, int scale) const
{
    if (!available() || scale <= 0)
        return std::nullopt;
    const auto* dst = impl_->find(target);
    if (!dst || !dst->layout.ready())
        return std::nullopt;

    // Multiple cursor images can intersect a seam simultaneously. Prefer the output
    // actually containing the hotspot (half-open bounds), then the latest position.
    std::pair<bool, uint64_t> best{};
    std::optional<std::pair<double, double>> result;
    for (const auto& entry : impl_->outputs)
    {
        const auto& out = entry.second;
        auto [width, height, transform] = out.mode;
        if (!out.position_order || !out.layout.ready() || width <= 0 || height <= 0 ||
            transform < WL_OUTPUT_TRANSFORM_NORMAL || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270)
            continue;
        // position is ALREADY transformed: swap the extent on quarter turns; do
        // not rotate or flip the point. wl_output.scale is not a fractional scale.
        const bool quarter_turn = transform == WL_OUTPUT_TRANSFORM_90 ||
                                  transform == WL_OUTPUT_TRANSFORM_270 ||
                                  transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
                                  transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
        const detail::GazeSourcePoint point{out.cursor_x, out.cursor_y, width, height,
                                            quarter_turn};
        const std::pair rank{point.inside(), out.position_order};
        if (rank <= best)
            continue;
        best = rank;
        result = point.relative_to(out.layout, dst->layout, scale);
    }
    return result;
}
void DesktopGaze::reset()
{
    if (!impl_)
        return;
    impl_->outputs.clear();
    if (impl_->layout_manager)
        zxdg_output_manager_v1_destroy(impl_->layout_manager);
    if (impl_->copy_manager)
        ext_image_copy_capture_manager_v1_destroy(impl_->copy_manager);
    if (impl_->source_manager)
        ext_output_image_capture_source_manager_v1_destroy(impl_->source_manager);
    impl_.reset();
}
} // namespace nekomote::layer_shell
