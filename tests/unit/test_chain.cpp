#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/chain.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

TEST_CASE("Two-particle chains match isolated original physical trajectories", "[runtime]")
{
    using namespace nekomote;
    using namespace nekomote::runtime;
    using Json = nlohmann::json;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/chain_reference.json");
    REQUIRE(input.good());
    const auto fixture = Json::parse(input);
    const double tolerance = fixture.at("value_tolerance").get<double>();
    const auto vec = [](const Json& v) { return Vec2{v.at(0).get<float>(), v.at(1).get<float>()}; };
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("name").get<std::string>());
        const auto& config = experiment.at("coefficients");
        const ChainCoefficients coefficients{
            config.at("stretch_stiffness").get<float>(), config.at("gravity").get<float>(),
            vec(config.at("friction")), config.at("length").get<std::array<float, 2>>(),
            config.at("normal_velocity_damping").get<float>()};
        ChainState state;
        ChainRest rest;
        for (std::size_t i = 0; i < 2; ++i)
        {
            state.position[i] = vec(experiment.at("state").at("position").at(i));
            state.velocity[i] = vec(experiment.at("state").at("velocity").at(i));
            rest.position[i] = vec(experiment.at("rest").at(i));
        }
        const auto& output = experiment.at("output");
        const ChainOutput mapping{output.at("scale_x").get<std::array<float, 2>>(),
                                  output.at("root_scale_y").get<float>(),
                                  output.at("vertical_offset").get<float>(),
                                  output.at("amplitude").get<float>()};
        const auto& bend = experiment.at("bend");
        const BendCoefficients bend_coefficients{bend.at("speed").get<float>(),
                                                 bend.at("volume").get<float>()};
        BendState bend_state{bend.at("phase").get<float>(), bend.at("gain").get<float>()};
        Vec2 external;
        for (const auto& sample : experiment.at("samples"))
        {
            const int step = sample.at("step").get<int>();
            INFO(step);
            for (const auto& event : experiment.at("events"))
                if (event.at("step").get<int>() == step)
                    external =
                        vec(event.at("forces").at(experiment.at("controller").get<std::string>()));
            const auto pose = advance_chain(state, coefficients, rest, {}, external);
            const auto actual =
                advance_bend(bend_state, bend_coefficients, unbent_chain_output(pose, mapping));
            const auto& expected = sample.at("values");
            REQUIRE_THAT(actual.root_x,
                         Catch::Matchers::WithinAbs(expected.at(0).get<double>(), tolerance));
            REQUIRE_THAT(actual.vertical,
                         Catch::Matchers::WithinAbs(expected.at(1).get<double>(), tolerance));
            REQUIRE_THAT(actual.tip_x,
                         Catch::Matchers::WithinAbs(expected.at(2).get<double>(), tolerance));
        }
    }
}
