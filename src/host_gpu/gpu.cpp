#include <nekomote/host_gpu/gpu.hpp>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "identity.hpp"
#include "pci_names.hpp"

namespace nekomote::host_gpu
{
namespace
{
std::string read(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    std::string value;
    std::getline(stream, value);
    return value;
}
unsigned hex(std::string_view value)
{
    if (value.starts_with("0x"))
        value.remove_prefix(2);
    unsigned result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid PCI sysfs hexadecimal value");
    return result;
}
void environment(const char* name, const char* value)
{
    const int result = value ? ::setenv(name, value, 1) : ::unsetenv(name);
    if (result != 0)
        throw std::system_error(errno, std::generic_category(), name);
}
std::string node_pci(const char* node)
{
    if (!node || !*node)
        return {};
    const auto name = std::filesystem::path(node).filename();
    std::error_code error;
    const auto device = std::filesystem::canonical(
        std::filesystem::path("/sys/class/drm") / name / "device", error);
    if (error)
        return {};
    try
    {
        return normalize_selector("pci:" + device.filename().string());
    }
    catch (const std::invalid_argument&)
    {
        return {};
    }
}
struct EglLibrary
{
    void* handle{::dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL)};
    ~EglLibrary()
    {
        if (handle)
            ::dlclose(handle);
    }
    Proc proc(const char* name) const
    {
        if (!handle)
            return nullptr;
        if (auto* result = ::dlsym(handle, name))
            return reinterpret_cast<Proc>(result);
        const auto get =
            reinterpret_cast<Proc (*)(const char*)>(::dlsym(handle, "eglGetProcAddress"));
        return get ? get(name) : nullptr;
    }
};
std::string egl_pci(void* display)
{
    if (!display)
        return {};
    EglLibrary library;
    const auto query = reinterpret_cast<unsigned (*)(void*, int, std::intptr_t*)>(
        library.proc("eglQueryDisplayAttribEXT"));
    const auto string =
        reinterpret_cast<const char* (*)(void*, int)>(library.proc("eglQueryDeviceStringEXT"));
    if (!query || !string)
        return {};
    std::intptr_t device{};
    if (!query(display, 0x322C /* EGL_DEVICE_EXT */, &device))
        return {};
    auto* native = reinterpret_cast<void*>(device);
    const char* extensions = string(native, 0x3055 /* EGL_EXTENSIONS */);
    const std::string_view names = extensions ? extensions : "";
    if (names.find("EGL_EXT_device_drm_render_node") != std::string_view::npos)
        if (auto pci = node_pci(string(native, 0x3377)); !pci.empty())
            return pci;
    if (names.find("EGL_EXT_device_drm") != std::string_view::npos)
        return node_pci(string(native, 0x3233));
    return {};
}
std::string nvidia_uuid_pci(Resolve resolve)
{
    const auto get = reinterpret_cast<void (*)(unsigned, unsigned, unsigned char*)>(
        resolve("glGetUnsignedBytei_vEXT"));
    const auto integer = reinterpret_cast<void (*)(unsigned, int*)>(resolve("glGetIntegerv"));
    if (!get || !integer)
        return {};
    // Only query EXT enums if the context advertises EXT_memory_object.
    const auto indexed =
        reinterpret_cast<const unsigned char* (*)(unsigned, unsigned)>(resolve("glGetStringi"));
    if (!indexed)
        return {};
    int count{};
    integer(0x821D /* GL_NUM_EXTENSIONS */, &count);
    bool supported = false;
    for (int i = 0; i < count; ++i)
    {
        const auto* name = indexed(0x1F03, static_cast<unsigned>(i));
        if (name && std::string_view(reinterpret_cast<const char*>(name)) == "GL_EXT_memory_object")
            supported = true;
    }
    if (!supported)
        return {};
    int devices{};
    integer(0x9596 /* GL_NUM_DEVICE_UUIDS_EXT */, &devices);
    if (devices != 1)
        return {};
    std::array<unsigned char, 16> uuid{};
    get(0x9597 /* GL_DEVICE_UUID_EXT */, 0, uuid.data());
    char formatted[48]{};
    std::snprintf(formatted, sizeof(formatted),
                  "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8],
                  uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    std::error_code error;
    const std::filesystem::directory_iterator entries("/proc/driver/nvidia/gpus", error);
    if (error)
        return {};
    for (const auto& entry : entries)
    {
        std::ifstream stream(entry.path() / "information");
        std::string line;
        while (std::getline(stream, line))
            if (line.starts_with("GPU UUID:") && line.find(formatted) != std::string::npos)
                return normalize_selector("pci:" + entry.path().filename().string());
    }
    return {};
}
} // namespace

std::string normalize_selector(std::string_view selector)
{
    constexpr std::string_view format = "pci:0000:00:00.0";
    if (selector.size() != format.size())
        throw std::invalid_argument("--gpu expects pci:DDDD:BB:DD.F");
    for (std::size_t i = 0; i < format.size(); ++i)
        if ((i < 4 || i == 8 || i == 11 || i == 14)
                ? selector[i] != format[i]
                : !std::isxdigit(static_cast<unsigned char>(selector[i])))
            throw std::invalid_argument("--gpu expects pci:DDDD:BB:DD.F");
    if (hex(selector.substr(12, 2)) > 31 || hex(selector.substr(15, 1)) > 7)
        throw std::invalid_argument("--gpu PCI device/function out of range");
    std::string result(selector);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
std::vector<Device> discover(const std::filesystem::path& pci_root)
{
    std::vector<Device> result;
    for (const auto& entry : std::filesystem::directory_iterator(pci_root))
    {
        const auto category = read(entry.path() / "class");
        if (category.empty() || (hex(category) >> 16) != 3)
            continue;
        Device device;
        device.selector = normalize_selector("pci:" + entry.path().filename().string());
        device.vendor = hex(read(entry.path() / "vendor"));
        device.device = hex(read(entry.path() / "device"));
        std::error_code error;
        const auto driver = std::filesystem::read_symlink(entry.path() / "driver", error);
        device.driver = error ? "unbound" : driver.filename().string();
        result.push_back(std::move(device));
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.selector < b.selector; });
    return result;
}
void list_gpus()
{
    std::puts("PCI display hardware (backend recognition and presentation are not verified):");
    const auto devices = discover();
    std::vector<std::string> names(devices.size(), "Unknown GPU");
    // Use the distribution database without requiring libpci, lspci or a graphics driver.
    for (const auto* path :
         {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"})
    {
        std::ifstream database(path);
        if (database)
        {
            names = detail::pci_names(devices, database);
            break;
        }
    }
    for (std::size_t i = 0; i < devices.size(); ++i)
    {
        const auto& gpu = devices[i];
        std::printf("%s  %s  vendor=%04x device=%04x driver=%s\n", gpu.selector.c_str(),
                    names[i].c_str(), gpu.vendor, gpu.device, gpu.driver.c_str());
    }
}
void prepare_opengl(std::string_view selector, GlPlatform platform)
{
    if (selector.empty())
        return;
    const auto canonical = normalize_selector(selector);
    const auto devices = discover();
    const auto chosen = std::find_if(devices.begin(), devices.end(),
                                     [&](const auto& d) { return d.selector == canonical; });
    if (chosen == devices.end())
        throw std::runtime_error("requested GPU does not exist: " + canonical);
    if (chosen->driver == "nvidia")
    {
        if (std::count_if(devices.begin(), devices.end(),
                          [](const auto& d) { return d.driver == "nvidia"; }) != 1)
            throw std::runtime_error("OpenGL selection of multiple NVIDIA GPUs requires a provider "
                                     "mapping; unsupported");
        environment("DRI_PRIME", nullptr);
        environment("__NV_PRIME_RENDER_OFFLOAD_PROVIDER", nullptr);
        environment("__NV_PRIME_RENDER_OFFLOAD", "1");
        if (platform == GlPlatform::Glx)
            environment("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
    }
    else if (chosen->driver == "amdgpu" || chosen->driver == "radeon" || chosen->driver == "i915" ||
             chosen->driver == "xe" || chosen->driver == "nouveau")
    {
        std::string prime = "pci-" + canonical.substr(4);
        std::replace(prime.begin(), prime.end(), ':', '_');
        std::replace(prime.begin(), prime.end(), '.', '_');
        environment("DRI_PRIME", prime.c_str());
        environment("__NV_PRIME_RENDER_OFFLOAD", nullptr);
        environment("__NV_PRIME_RENDER_OFFLOAD_PROVIDER", nullptr);
        if (platform == GlPlatform::Glx)
            environment("__GLX_VENDOR_LIBRARY_NAME", "mesa");
    }
    else
        throw std::runtime_error("OpenGL GPU selection unsupported for driver " + chosen->driver);
}
void verify_opengl(std::string_view selector, Resolve resolve, void* egl_display)
{
    if (!resolve)
        throw std::invalid_argument("OpenGL resolver is null");
    const auto get = reinterpret_cast<const unsigned char* (*)(unsigned)>(resolve("glGetString"));
    const auto* name = get ? get(0x1F01 /* GL_RENDERER */) : nullptr;
    if (!name)
        throw std::runtime_error("OpenGL context is not current");
    const std::string renderer(reinterpret_cast<const char*>(name));
    std::string actual;
    try
    {
        const auto uuid_pci = nvidia_uuid_pci(resolve);
        const auto display_pci = egl_pci(egl_display);
        if (!uuid_pci.empty() && !display_pci.empty() && uuid_pci != display_pci)
            std::fprintf(stderr, "[host_gpu] context GPU=%s differs from EGL display GPU=%s\n",
                         uuid_pci.c_str(), display_pci.c_str());
        actual = detail::context_identity(uuid_pci, display_pci, selector);
        if (actual.empty() && !egl_display)
        {
            const auto query = reinterpret_cast<int (*)(int, unsigned*)>(
                resolve("glXQueryCurrentRendererIntegerMESA"));
            unsigned vendor{}, device{};
            if (query && query(0x8183, &vendor) && query(0x8184, &device))
                actual = detail::unique_pci_identity(discover(), vendor, device);
        }
    }
    catch (const std::exception& error)
    {
        if (!selector.empty())
            throw;
        std::fprintf(stderr, "[host_gpu] optional identity query unavailable: %s\n", error.what());
    }
    std::fprintf(stderr, "[host_gpu] backend=opengl renderer=%s device=%s\n", renderer.c_str(),
                 actual.empty() ? "unverified" : actual.c_str());
    detail::verify_identity(selector, actual);
}
} // namespace nekomote::host_gpu
