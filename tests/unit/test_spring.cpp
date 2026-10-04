#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/spring.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

TEST_CASE("Spring response matches isolated original physical controller trajectories", "[runtime]")
{
    using namespace nekomote;
    using namespace nekomote::runtime;
    using Json = nlohmann::json;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/spring_reference.json");
    REQUIRE(input.good());
    const auto fixture = Json::parse(input);
    const auto tolerance = fixture.at("value_tolerance").get<double>();
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("name").get<std::string>());
        const auto& config = experiment.at("coefficients");
        const SpringCoefficients coefficients{config.at("spring").get<float>(),
                                              config.at("friction").get<float>(),
                                              config.at("gravity").get<float>()};
        SpringState state;
        // The source imports these snapshot components into Float32 storage.
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            state.position[axis] = experiment.at("state").at("position").at(axis).get<float>();
            state.velocity[axis] = experiment.at("state").at("velocity").at(axis).get<float>();
        }
        const auto& mapping = experiment.at("output");
        const SpringOutput output{{mapping.at("axis_scale").at(0).get<float>(),
                                   mapping.at("axis_scale").at(1).get<float>()},
                                  mapping.at("offset").get<float>(),
                                  mapping.at("amplitude").get<float>()};
        Vec2 outer;
        for (const auto& sample : experiment.at("samples"))
        {
            const int step = sample.at("step").get<int>();
            INFO(step);
            for (const auto& event : experiment.at("events"))
                if (event.at("step").get<int>() == step)
                {
                    REQUIRE(event.contains("forces"));
                    const auto& force = event.at("forces").at("bust");
                    outer = {force.at(0).get<float>(), force.at(1).get<float>()};
                }
            const auto deformation = advance_spring(state, coefficients, {}, outer);
            const auto actual = spring_output(deformation, output);
            if (!sample.at("value").at(0).is_null())
                REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(
                                           sample.at("value").at(0).get<double>(), tolerance));
            if (!sample.at("value").at(1).is_null())
                REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(
                                           sample.at("value").at(1).get<double>(), tolerance));
        }
    }
}

TEST_CASE("Spring particle storage and transient output match observed author step", "[runtime]")
{
    using namespace nekomote::runtime;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/spring_storage_reference.json");
    REQUIRE(input.good());
    const auto reference = nlohmann::json::parse(input);
    const auto read = [&](std::size_t index)
    {
        for (const auto& entry : reference.at("reads"))
            if (entry.at("index").get<std::size_t>() == index)
                return entry.at("value").get<float>();
        throw std::runtime_error("missing observed spring input");
    };
    const auto stored = [&](std::size_t index)
    {
        for (const auto& entry : reference.at("stores"))
            if (entry.at("index").get<std::size_t>() == index)
                return entry.at("stored").get<float>();
        throw std::runtime_error("missing observed spring output");
    };
    SpringState state;
    state.position = {read(18489667), read(18489668)};
    state.velocity = {read(18489670), read(18489671)};
    state.origin_offset = {read(18489664), read(18489665)};
    state.initialized = true;
    const auto deformation = advance_spring(
        state, {read(18489656), read(18489657), read(18489655)},
        {stored(18489661) - state.origin_offset.x, stored(18489662) - state.origin_offset.y}, {});
    REQUIRE(state.anchor_offset.x == stored(18489661));
    REQUIRE(state.anchor_offset.y == stored(18489662));
    REQUIRE(state.position[0] == stored(18489667));
    REQUIRE(state.position[1] == stored(18489668));
    REQUIRE(state.velocity[0] == stored(18489670));
    REQUIRE(state.velocity[1] == stored(18489671));
    const auto output =
        spring_output(deformation, {{read(18489658), read(18489659)}, read(18489673), 1});
    REQUIRE(output.x == stored(10073));
    REQUIRE(output.y == stored(10072));
}
