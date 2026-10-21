#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nekomote::application
{

// UI and asset-name languages. The stored code matches the keys used by the
// localized name dictionaries in the asset JSON files.
enum class Language
{
    English,
    ChineseSimplified,
    ChineseTraditional,
    Japanese,
};

constexpr std::size_t kLanguageCount = 4;

std::string_view language_code(Language lang) noexcept;
std::optional<Language> language_from_code(std::string_view code) noexcept;

// A display name with one entry per supported language. Asset name
// dictionaries originate from zh-CN data, so resolve() falls back to zh-CN
// first when the requested translation is missing, then to the remaining
// languages, and finally to the caller-provided fallback (asset id).
struct LocalizedName
{
    std::string en{};
    std::string zh_cn{};
    std::string zh_tw{};
    std::string ja{};

    bool empty() const noexcept
    {
        return en.empty() && zh_cn.empty() && zh_tw.empty() && ja.empty();
    }

    const std::string* field_for(Language lang) const noexcept
    {
        switch (lang)
        {
            case Language::English:
                return &en;
            case Language::ChineseSimplified:
                return &zh_cn;
            case Language::ChineseTraditional:
                return &zh_tw;
            case Language::Japanese:
                return &ja;
        }
        return &en;
    }

    const std::string& resolve(Language lang, const std::string& fallback) const noexcept
    {
        const std::string* direct = field_for(lang);
        if (!direct->empty())
            return *direct;
        for (std::size_t i = 0; i < kLanguageCount; ++i)
        {
            const auto chain_lang = static_cast<Language>(i);
            const std::string* candidate = field_for(chain_lang);
            if (candidate != direct && !candidate->empty())
                return *candidate;
        }
        return fallback;
    }
};

struct CharacterEntry
{
    std::string name{};
    std::filesystem::path model_path{};
    std::filesystem::path session_path{};
    std::filesystem::path directory{};
    float cutoff_y{0.0f};
    bool has_cutoff{false};
    LocalizedName names{};
    bool hidden{false};
    int order{0};

    CharacterEntry() = default;
    CharacterEntry(std::string n, std::filesystem::path mp, std::filesystem::path sp,
                   std::filesystem::path dir)
        : name(std::move(n)), model_path(std::move(mp)), session_path(std::move(sp)),
          directory(std::move(dir))
    {
    }
};

struct CharacterSubEntry
{
    int character_index{-1};
    std::string model_id{};
    LocalizedName names{};
    std::string fallback_name{};
};

struct CharacterGroup
{
    std::string id{};
    LocalizedName title{};
    std::vector<CharacterSubEntry> items{};
};

enum class BackgroundType
{
    Static,
    Dynamic,
};

struct BackgroundPhase
{
    std::filesystem::path image{};
    int start_minutes{0};
};

struct BackgroundEntry
{
    std::string name{};
    LocalizedName names{};
    BackgroundType type{BackgroundType::Static};
    std::vector<BackgroundPhase> phases{};
    float transition_seconds{1.8f};

    BackgroundEntry() = default;
    BackgroundEntry(std::string n, LocalizedName dn, std::filesystem::path p)
        : name(std::move(n)), names(std::move(dn)), type(BackgroundType::Static),
          phases{{std::move(p), 0}}
    {
    }
    BackgroundEntry(std::string n, std::filesystem::path p)
        : name(std::move(n)), type(BackgroundType::Static), phases{{std::move(p), 0}}
    {
    }
    const std::filesystem::path& path() const noexcept;
    std::size_t phase_index_at(int minutes_since_midnight) const noexcept;
    const std::filesystem::path& path_for_phase(std::size_t index) const noexcept;
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
    Language language{Language::English};
    float scale{1};
    float origin_x{0};
    float origin_y{400};
    int fps{60};
    bool mute{false};
    bool lock_transform{false};
    bool fit{false};
    bool transform_saved{false};
    bool icon_position_saved{false};
    float icon_x{0};
    float icon_y{8};
    bool sidebar_position_saved{false};
    float sidebar_x{8};
    float sidebar_y{8};
    bool sidebar_free{false};
    float background_fade_seconds{1.0f};
    static WallpaperConfig load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};
} // namespace nekomote::application
