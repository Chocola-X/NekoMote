#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace nekomote::application
{
enum class LaunchKind
{
    Player,
    X11,
    LayerShell
};

struct LaunchOptions
{
    std::string assets_dir;
    std::string model_path;
    std::string session_path;
    std::string output;
    std::string backend{"opengl"};
    std::string input{"interactive"};
    std::string state_path;
    std::uint32_t width{1920};
    std::uint32_t height{1080};
};

// Config location resolution performs no writes. Relative XDG_CONFIG_HOME values
// are ignored, as specified by XDG; HOME must be absolute for the fallback.
std::filesystem::path config_directory();
std::filesystem::path default_panel_config_path();

class LaunchConfig
{
  public:
    static LaunchConfig load(LaunchKind kind, const std::filesystem::path& path = {});
    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }
    const LaunchOptions& values() const noexcept
    {
        return values_;
    }
    // Explicit persistence only. Paths supplied here resolve against cwd;
    // paths read from a file resolve against that file's directory.
    void save(LaunchOptions values);

  private:
    std::filesystem::path path_;
    LaunchOptions values_;
};
} // namespace nekomote::application
