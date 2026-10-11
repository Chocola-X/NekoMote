#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nekomote::application
{
struct CharacterEntry
{
    std::string name{};
    std::filesystem::path model_path{};
    std::filesystem::path session_path{};
    std::filesystem::path directory{};
    float cutoff_y{0.0f};
    bool has_cutoff{false};
    std::string display_name{};

    CharacterEntry() = default;
    CharacterEntry(std::string n, std::filesystem::path mp, std::filesystem::path sp,
                   std::filesystem::path dir, float cy = 0.0f, bool hc = false, std::string dn = {})
        : name(std::move(n)), model_path(std::move(mp)), session_path(std::move(sp)),
          directory(std::move(dir)), cutoff_y(cy), has_cutoff(hc), display_name(std::move(dn))
    {
    }
};

struct CharacterSubEntry
{
    int character_index{-1};
    std::string model_id{};
    std::string display_name{};
};

struct CharacterGroup
{
    std::string id{};
    std::string title{};
    std::vector<CharacterSubEntry> items{};
};

enum class BackgroundType
{
    Static,
    Dynamic,
};

struct BackgroundEntry
{
    std::string name{};
    std::string display_name{};
    BackgroundType type{BackgroundType::Static};
    std::filesystem::path path_a{};
    std::filesystem::path path_b{};
    std::filesystem::path path_c{};

    BackgroundEntry() = default;
    BackgroundEntry(std::string n, std::string dn, std::filesystem::path p)
        : name(std::move(n)), display_name(std::move(dn)), type(BackgroundType::Static),
          path_a(std::move(p))
    {
    }
    BackgroundEntry(std::string n, std::filesystem::path p)
        : name(n), display_name(std::move(n)), type(BackgroundType::Static), path_a(std::move(p))
    {
    }
    const std::filesystem::path& path() const noexcept { return path_a; }
    const std::filesystem::path& path_for_phase(char phase) const noexcept
    {
        switch (phase)
        {
            case 'B': return path_b;
            case 'C': return path_c;
            default: return path_a;
        }
    }
};

struct AssetCatalog
{
    std::vector<CharacterEntry> characters;
    std::vector<CharacterGroup> character_groups;
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
} // namespace nekomote::application
