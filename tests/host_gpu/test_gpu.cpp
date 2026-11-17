#include <nekomote/host_gpu/gpu.hpp>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "identity.hpp"

namespace
{
void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("GPU host contract failed");
}
void reject(const std::string& selector)
{
    try
    {
        (void)nekomote::host_gpu::normalize_selector(selector);
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("invalid GPU selector accepted: " + selector);
}
} // namespace
int main()
{
    namespace gpu = nekomote::host_gpu;
    namespace fs = std::filesystem;
    try
    {
        require(gpu::normalize_selector("pci:ABCD:EF:1F.7") == "pci:abcd:ef:1f.7");
        for (const auto* input : {"", "0", "NVIDIA", "pci:0000:01:20.0", "pci:0000:01:00.8",
                                  "pci:0000:01:00.0!", "pci:0:01:00.0", "pci:0000:gg:00.0"})
            reject(input);
        char pattern[] = "/tmp/nekomote-host-gpu.XXXXXX";
        const auto* created = ::mkdtemp(pattern);
        require(created != nullptr);
        struct Fixture
        {
            fs::path root;
            ~Fixture()
            {
                std::error_code error;
                fs::remove_all(root, error);
            }
        } fixture{created};
        const auto add =
            [&](const char* address, const char* category, const char* vendor, const char* device)
        {
            const auto path = fixture.root / address;
            fs::create_directory(path);
            std::ofstream(path / "class") << category;
            std::ofstream(path / "vendor") << vendor;
            std::ofstream(path / "device") << device;
            return path;
        };
        add("0000:06:00.0", "0x030000", "0x1002", "0x1638");
        const auto first = add("0000:01:00.0", "0x030200", "0x10de", "0x2560");
        add("0000:00:02.0", "0x020000", "0x1234", "0x5678");
        fs::create_directory_symlink("../../drivers/nvidia", first / "driver");
        const auto devices = gpu::discover(fixture.root);
        require(devices.size() == 2);
        require(devices[0].selector == "pci:0000:01:00.0");
        require(devices[0].vendor == 0x10de && devices[0].device == 0x2560 &&
                devices[0].driver == "nvidia");
        require(devices[1].driver == "unbound");
        require(gpu::detail::unique_pci_identity(devices, 0x10de, 0x2560) == "pci:0000:01:00.0");
        auto duplicate = devices;
        duplicate.push_back({"pci:0000:02:00.0", 0x10de, 0x2560, "nvidia"});
        require(gpu::detail::unique_pci_identity(duplicate, 0x10de, 0x2560).empty());
        require(gpu::detail::unique_pci_identity(devices, 0xabcd, 0x1234).empty());
        require(gpu::detail::context_identity("pci:0000:01:00.0", "", "pci:0000:01:00.0") ==
                "pci:0000:01:00.0");
        require(gpu::detail::context_identity("", "pci:0000:06:00.0", "pci:0000:06:00.0") ==
                "pci:0000:06:00.0");
        require(gpu::detail::context_identity("pci:0000:01:00.0", "pci:0000:06:00.0", "") ==
                "pci:0000:01:00.0");
        bool contradictory = false;
        try
        {
            (void)gpu::detail::context_identity("pci:0000:01:00.0", "pci:0000:06:00.0",
                                                "pci:0000:01:00.0");
        }
        catch (const std::runtime_error&)
        {
            contradictory = true;
        }
        require(contradictory);
        gpu::detail::verify_identity("", "");
        gpu::detail::verify_identity("pci:0000:01:00.0", "pci:0000:01:00.0");
        for (const auto* identity : {"", "pci:0000:06:00.0"})
        {
            bool failed = false;
            try
            {
                gpu::detail::verify_identity("pci:0000:01:00.0", identity);
            }
            catch (const std::runtime_error&)
            {
                failed = true;
            }
            require(failed);
        }
        // Unspecified selection must not touch external offload settings.
        ::setenv("DRI_PRIME", "external-selection", 1);
        ::setenv("__NV_PRIME_RENDER_OFFLOAD", "external-offload", 1);
        gpu::prepare_opengl("", gpu::GlPlatform::Egl);
        require(std::string(std::getenv("DRI_PRIME")) == "external-selection");
        require(std::string(std::getenv("__NV_PRIME_RENDER_OFFLOAD")) == "external-offload");
        reject("pci:0000:01:00.0\n");
        try
        {
            gpu::prepare_opengl("pci:ffff:ff:1f.7", gpu::GlPlatform::Glx);
            throw std::logic_error("missing GPU accepted");
        }
        catch (const std::runtime_error&)
        {
        }
        std::puts("host_gpu selector, discovery and initialization contracts passed");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
