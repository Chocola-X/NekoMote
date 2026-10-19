#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
using Json = nlohmann::json;
struct SessionFile
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("nekomote-session-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    ~SessionFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    void write(const Json& j) const
    {
        std::ofstream f(path);
        f << j;
    }
};
Json session_document()
{
    const Json action{
        {"main_timeline", "喜ぶ00"},
        {"difference_slots", {"", "", "", "idle", "", ""}},
        {"variables",
         {{{"label", "head_LR"}, {"target", 10}, {"author_frames", 7.2}, {"easing", -1}}}}};
    const Json regions{{"bust", {{"center", {0, 10}}, {"radius", {40, 20}}}},
                       {"eye", nullptr},
                       {"face", nullptr},
                       {"head", nullptr},
                       {"pant", nullptr}};
    const Json reaction{{"label", "test/voice"},
                        {"zones", {"bust", "head"}},
                        {"action", action},
                        {"recovery", action},
                        {"audio_path", "audio/voice.flac"},
                        {"duration_ticks", 60},
                        {"recovery_ticks", 12},
                        {"beats", {{{"author_tick", 10}, {"action", action}}}}};
    const Json session{{"model", "test"},
                       {"idle_timeline", "idle"},
                       {"talk_max", 5},
                       {"interaction", {{"eye", -100}, {"regions", regions}}},
                       {"reactions", {reaction}}};
    return {{"schema", "nekomote.session"}, {"version", 1}, {"session", session}};
}
} // namespace

TEST_CASE("Session assets preserve authored bindings and reject corrupt rules", "[asset][session]")
{
    SessionFile file;
    auto j = session_document();
    SECTION("valid")
    {
        file.write(j);
        const auto result = nekomote::asset_io::load_session_asset(file.path);
        REQUIRE(result.ok);
        REQUIRE(result.asset.reactions.size() == 1);
        REQUIRE(result.asset.interaction.eye == -100);
        REQUIRE(result.asset.reactions[0].action.main_timeline == "喜ぶ00");
        REQUIRE(result.asset.reactions[0].action.variables[0].author_frames == 7.2f);
        REQUIRE(result.asset.reactions[0].beats[0].author_tick == 10);
        return;
    }
    SECTION("legacy WAVE audio reference")
    {
        j["session"]["reactions"][0]["audio_path"] = "audio/voice.wav";
    }
    SECTION("version")
    {
        j["version"] = 0;
    }
    SECTION("fractional duration")
    {
        j["session"]["reactions"][0]["duration_ticks"] = 2.5;
    }
    SECTION("negative radius")
    {
        j["session"]["interaction"]["regions"]["bust"]["radius"][0] = -1;
    }
    SECTION("duplicate reaction")
    {
        j["session"]["reactions"].push_back(j["session"]["reactions"][0]);
    }
    SECTION("unreachable beat")
    {
        j["session"]["reactions"][0]["beats"][0]["author_tick"] = 60;
    }
    SECTION("duplicate zone")
    {
        j["session"]["reactions"][0]["zones"] = {"bust", "bust"};
    }
    SECTION("unknown zone")
    {
        j["session"]["reactions"][0]["zones"] = {"unknown"};
    }
    SECTION("escaped audio")
    {
        j["session"]["reactions"][0]["audio_path"] = "audio/../../voice.flac";
    }
    SECTION("absolute audio")
    {
        j["session"]["reactions"][0]["audio_path"] = "/voice.flac";
    }
    SECTION("wrong slot count")
    {
        j["session"]["reactions"][0]["action"]["difference_slots"] = {"idle"};
    }
    file.write(j);
    const auto result = nekomote::asset_io::load_session_asset(file.path);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.asset.reactions.empty());
    REQUIRE_FALSE(result.errors.empty());
    REQUIRE(result.errors[0].find(file.path.string()) != std::string::npos);
}
