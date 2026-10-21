#include "config_io.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace nekomote::application::config_io
{
nlohmann::json read(const std::filesystem::path& path)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
        throw std::filesystem::filesystem_error("cannot inspect config", path, error);
    if (!exists)
        return nlohmann::json(nlohmann::json::value_t::discarded);
    if (!std::filesystem::is_regular_file(path))
        throw std::system_error(std::make_error_code(std::errc::invalid_argument),
                                path.string() + ": config must be a regular file");
    std::ifstream input(path);
    if (!input)
        throw std::system_error(std::make_error_code(std::errc::io_error),
                                path.string() + ": cannot read config");
    try
    {
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& value)
        {
            if (event == nlohmann::json::parse_event_t::object_start)
                object_keys.emplace_back();
            else if (event == nlohmann::json::parse_event_t::object_end)
                object_keys.pop_back();
            else if (event == nlohmann::json::parse_event_t::key &&
                     !object_keys.back().insert(value.get<std::string>()).second)
                throw std::invalid_argument(path.string() + ": duplicate config key '" +
                                            value.get<std::string>() + "'");
            return true;
        };
        auto document = nlohmann::json::parse(input, callback);
        if (input.bad())
            throw std::system_error(std::make_error_code(std::errc::io_error),
                                    path.string() + ": cannot finish reading config");
        return document;
    }
    catch (const nlohmann::json::exception& parse_error)
    {
        throw std::invalid_argument(path.string() + ": invalid config JSON: " + parse_error.what());
    }
}

void validate_keys(const nlohmann::json& document, std::initializer_list<std::string_view> keys,
                   const std::filesystem::path& path)
{
    const auto fail = [&](const std::string& message)
    { throw std::invalid_argument(path.string() + ": " + message); };
    if (!document.is_object())
        fail("config must be an object");
    if (!document.contains("version") || !document["version"].is_number_integer() ||
        document["version"] != 1)
        fail("unsupported config version; recreate the file using the current version (1)");
    for (auto it = document.begin(); it != document.end(); ++it)
    {
        bool known = it.key() == "version";
        for (auto key : keys)
            known = known || it.key() == key;
        if (!known)
            fail("unknown config key '" + it.key() + "'");
    }
}

void write_atomic(const std::filesystem::path& path, const nlohmann::json& document)
{
    if (path.empty() || !path.has_filename() || path.filename() == "." || path.filename() == "..")
        throw std::invalid_argument("config path must name a file");
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    std::filesystem::create_directories(resolved.parent_path());
    const auto content = document.dump(2) + "\n";
    auto pattern =
        (resolved.parent_path() / ("." + resolved.filename().string() + ".XXXXXX")).string();
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int fd = ::mkstemp(temporary.data());
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(),
                                "cannot create config temporary " + pattern);
    struct Pending
    {
        int fd;
        const char* path;
        ~Pending()
        {
            if (fd >= 0)
                ::close(fd);
            ::unlink(path);
        }
    } pending{fd, temporary.data()};
    std::size_t offset = 0;
    while (offset < content.size())
    {
        const auto written = ::write(fd, content.data() + offset, content.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            throw std::system_error(written < 0 ? errno : EIO, std::generic_category(),
                                    "cannot write config " + resolved.string());
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0)
        throw std::system_error(errno, std::generic_category(),
                                "cannot sync config " + resolved.string());
    const int close_result = ::close(fd);
    pending.fd = -1;
    if (close_result != 0)
        throw std::system_error(errno, std::generic_category(),
                                "cannot close config " + resolved.string());
    std::filesystem::rename(temporary.data(), resolved);
}
} // namespace nekomote::application::config_io
