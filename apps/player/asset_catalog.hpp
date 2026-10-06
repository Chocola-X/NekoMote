#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nekomote::player
{
struct CharacterEntry
{
    std::string name;
    std::filesystem::path model_path;
    std::filesystem::path session_path;
    std::filesystem::path directory;
    float cutoff_y{0};
    bool has_cutoff{false};
};

struct BackgroundEntry
{
    std::string name;
    std::filesystem::path path;
};

struct AssetCatalog
{
    std::vector<CharacterEntry> characters;
    std::vector<BackgroundEntry> backgrounds;
    std::filesystem::path assets_root;
    static AssetCatalog scan(const std::filesystem::path& assets_dir);
    const CharacterEntry* find_character(const std::string& name) const;
    const BackgroundEntry* find_background(const std::string& name) const;
};

struct WallpaperConfig
{
    std::string character;
    std::string background;
    float scale{1};
    float origin_x{0};
    float origin_y{400};
    int width{800};
    int height{1080};
    int fps{30};
    bool mute{false};
    bool fit{false};
    static WallpaperConfig load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};
}
