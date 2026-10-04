#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/bend.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

TEST_CASE("Chain output bending matches isolated SDK thresholds and phase wrapping", "[runtime]")
{
    using namespace nekomote::runtime;
    using Json = nlohmann::json;
    std::ifstream input(std::string(NEKOMOTE_FIXTURES_DIR) + "/bend_reference.json");
    REQUIRE(input.good());
    const auto fixture = Json::parse(input);
    const double tolerance = fixture.at("value_tolerance").get<double>();
    for (const auto& experiment : fixture.at("cases"))
    {
        INFO(experiment.at("name").get<std::string>());
        BendState state{experiment.at("phase").get<float>(), 0};
        const BendCoefficients coefficients{experiment.at("speed").get<float>(),
                                            experiment.at("volume").get<float>()};
        for (const auto& sample : experiment.at("samples"))
        {
            INFO(sample.at("step").get<int>());
            const auto& variables = sample.at("unbent");
            const auto actual =
                advance_bend(state, coefficients,
                             {variables.at(0).get<float>(), variables.at(1).get<float>(),
                              variables.at(2).get<float>()});
            const auto& expected = sample.at("output");
            REQUIRE_THAT(actual.root_x,
                         Catch::Matchers::WithinAbs(expected.at(0).get<double>(), tolerance));
            REQUIRE_THAT(actual.vertical,
                         Catch::Matchers::WithinAbs(expected.at(1).get<double>(), tolerance));
            REQUIRE_THAT(actual.tip_x,
                         Catch::Matchers::WithinAbs(expected.at(2).get<double>(), tolerance));
            REQUIRE(state.phase == sample.at("observed_phase_argument").get<float>());
        }
    }
}
