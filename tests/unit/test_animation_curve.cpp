#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/animation_curve.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("Transition curves match frozen original variable trajectories", "[runtime][animation]")
{
    std::ifstream input(std::filesystem::path{NEKOMOTE_FIXTURES_DIR} /
                        "animation_curve_reference.json");
    REQUIRE(input);
    const auto reference = nlohmann::json::parse(input);
    const float tolerance = reference.at("normalized_tolerance").get<float>();
    for (const auto& experiment : reference.at("experiments"))
    {
        INFO(experiment.at("case").get<std::string>());
        const float easing = experiment.at("easing").get<float>();
        for (const auto& sample : experiment.at("samples"))
        {
            const float progress = sample.at("progress").get<float>();
            const float expected = sample.at("observed_normalized").get<float>();
            REQUIRE_THAT(nekomote::runtime::ease_progress(progress, easing),
                         Catch::Matchers::WithinAbs(expected, tolerance));
        }
    }
    REQUIRE(nekomote::runtime::ease_progress(-1.0f, -2.5f) == 0.0f);
    REQUIRE(nekomote::runtime::ease_progress(2.0f, 2.5f) == 1.0f);
}
