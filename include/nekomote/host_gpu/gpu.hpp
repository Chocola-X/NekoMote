#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nekomote::host_gpu
{
enum class GlPlatform
{
    Glx,
    Egl
};
struct Device
{
    std::string selector;
    unsigned vendor{};
    unsigned device{};
    std::string driver;
};
using Proc = void (*)();
using Resolve = Proc (*)(const char*);
// Strict Linux PCI domain:bus:device.function; returns lowercase canonical form.
std::string normalize_selector(std::string_view selector);
// PCI display-class hardware only. Discovery does not prove graphics API or presentation support.
std::vector<Device> discover(const std::filesystem::path& pci_root = "/sys/bus/pci/devices");
void list_gpus();
// Call before initializing any window library/driver. Empty selector leaves the environment intact.
void prepare_opengl(std::string_view selector, GlPlatform platform);
// A context must be current. Explicit selection fails unless actual identity can be verified.
// Empty selection logs the renderer and any available identity without restricting default
// behavior.
void verify_opengl(std::string_view selector, Resolve resolve, void* egl_display = nullptr);
} // namespace nekomote::host_gpu
