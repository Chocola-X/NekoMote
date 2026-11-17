#pragma once

#include <nekomote/host_gpu/gpu.hpp>

#include <algorithm>
#include <stdexcept>

namespace nekomote::host_gpu::detail
{
// GLX_MESA_query_renderer has no PCI address. It proves identity only when the
// observed numeric vendor/device pair uniquely identifies the inventoried hardware.
inline std::string unique_pci_identity(const std::vector<Device>& devices, unsigned vendor,
                                       unsigned device)
{
    const auto matches = [&](const Device& d) { return d.vendor == vendor && d.device == device; };
    if (std::count_if(devices.begin(), devices.end(), matches) != 1)
        return {};
    return std::find_if(devices.begin(), devices.end(), matches)->selector;
}
inline std::string context_identity(std::string_view uuid_pci, std::string_view display_pci,
                                    std::string_view requested)
{
    if (!uuid_pci.empty() && !display_pci.empty() && uuid_pci != display_pci && !requested.empty())
        throw std::runtime_error("OpenGL context GPU " + std::string(uuid_pci) +
                                 " differs from EGL display GPU " + std::string(display_pci));
    return std::string(uuid_pci.empty() ? display_pci : uuid_pci);
}
inline void verify_identity(std::string_view selector, std::string_view actual)
{
    if (selector.empty())
        return;
    const auto target = normalize_selector(selector);
    if (actual.empty())
        throw std::runtime_error("cannot reliably verify OpenGL PCI identity for requested " +
                                 target);
    if (actual != target)
        throw std::runtime_error("OpenGL selected " + std::string(actual) +
                                 " instead of requested " + target);
}
} // namespace nekomote::host_gpu::detail
