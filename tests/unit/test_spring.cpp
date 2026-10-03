#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/spring.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
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
            if (step > 1)
                advance_spring(state, coefficients, {}, outer);
            const auto actual = spring_output(state, output);
            if (!sample.at("value").at(0).is_null())
                REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(
                                           sample.at("value").at(0).get<double>(), tolerance));
            if (!sample.at("value").at(1).is_null())
                REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(
                                           sample.at("value").at(1).get<double>(), tolerance));
        }
    }
}
