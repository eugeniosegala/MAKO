/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tools/temporal_sequence.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main() {
    using namespace mako::cli::quality;
    const auto require = [](const bool good) { if (!good) std::exit(1); };
    const auto plan = parseSequence("0.5;history;0.2,0.4,0.6,0.8;0.25,0.75");
    require(plan.size() == 4 && plan[1].empty() && plan[2].size() == 4);
    for (const auto text : {"", ";0.5", "0.5;", "history,0.5", "0", "1", "nan",
            "inf", "-0.1", "0.5,0.5", "0.75,0.25", "0.5,", "0.5junk",
            "0.1,0.2,0.3,0.4,0.5"}) {
        bool rejected = false;
        try { static_cast<void>(parseSequence(text)); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
    }
    std::string huge = "0.5";
    for (size_t i = 1; i < 241; ++i) huge += ";0.5";
    bool rejected = false;
    try { static_cast<void>(parseSequence(huge)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
    require(sequenceSceneTime(0) == 0 && sequenceSceneTime(12) == 1 &&
        sequenceSceneTime(24) == 0);
    bool differsAfterSix = false;
    for (size_t frame = 1; frame < 96; ++frame) {
        const auto time = sequenceSceneTime(frame);
        require(time >= 0 && time <= 1);
        require(std::abs(time - sequenceSceneTime(frame - 1)) < 0.084F);
        differsAfterSix = differsAfterSix || time != sequenceSceneTime(frame + 6);
    }
    require(differsAfterSix);
    std::cout << "Temporal sequence parsing and continuous scene clock passed\n";
}
