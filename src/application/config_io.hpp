#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace nekomote::application::config_io
{
nlohmann::json read(const std::filesystem::path& path);
void validate_keys(const nlohmann::json& document, std::initializer_list<std::string_view> keys,
                   const std::filesystem::path& path);
void write_atomic(const std::filesystem::path& path, const nlohmann::json& document);
} // namespace nekomote::application::config_io
