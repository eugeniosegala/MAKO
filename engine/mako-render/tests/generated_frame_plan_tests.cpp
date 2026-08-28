/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "generated_frame_plan.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

using namespace mako::layer;

namespace {

    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    void expectNear(const float actual, const float expected,
            const std::string_view message) {
        expect(std::abs(actual - expected) < 0.0001F, message);
    }

    template<typename Operation>
    void expectInvalid(Operation&& operation, const std::string_view message) {
        bool rejected = false;
        try {
            operation();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(rejected, message);
    }
}

int main() {
    static_assert(std::is_trivially_copyable_v<GeneratedFramePlan>);

    const auto empty = GeneratedFramePlan::evenlySpaced(0);
    expect(empty.empty() && empty.timestamps().empty(),
        "zero generated frames did not produce an empty plan");

    const auto fourX = GeneratedFramePlan::evenlySpaced(3);
    expect(fourX.size() == 3, "4x plan lost a generated frame");
    expectNear(fourX[0], 0.25F, "4x first timestamp changed");
    expectNear(fourX[1], 0.50F, "4x midpoint changed");
    expectNear(fourX[2], 0.75F, "4x final timestamp changed");

    const auto fiveX = GeneratedFramePlan::evenlySpaced(4);
    expect(fiveX.size() == 4, "5x plan lost a generated frame");
    expectNear(fiveX[0], 0.20F, "5x first timestamp changed");
    expectNear(fiveX[1], 0.40F, "5x second timestamp changed");
    expectNear(fiveX[2], 0.60F, "5x third timestamp changed");
    expectNear(fiveX[3], 0.80F, "5x final timestamp changed");

    const std::array<float, 3> explicitValues{0.20F, 0.55F, 0.90F};
    const auto explicitPlan = GeneratedFramePlan::fromTimestamps(
        std::span<const float>(explicitValues)
    );
    const auto fullyAdmitted = scheduleAdmittedGeneratedFrames(explicitPlan, 3);
    expect(fullyAdmitted == explicitPlan,
        "full admission reconstructed and lost explicit timestamps");

    const auto twoAdmitted = scheduleAdmittedGeneratedFrames(explicitPlan, 2);
    expect(twoAdmitted.size() == 2,
        "partial two-frame admission returned the wrong count");
    expectNear(twoAdmitted[0], 1.0F / 3.0F,
        "partial admission did not redistribute its first frame");
    expectNear(twoAdmitted[1], 2.0F / 3.0F,
        "partial admission did not redistribute its second frame");

    const auto oneAdmitted = scheduleAdmittedGeneratedFrames(explicitPlan, 1);
    expect(oneAdmitted.size() == 1,
        "partial one-frame admission returned the wrong count");
    expectNear(oneAdmitted.front(), 0.5F,
        "partial one-frame admission did not retain midpoint pacing");

    expect(scheduleAdmittedGeneratedFrames(explicitPlan, 0).empty(),
        "zero admission did not produce an empty scheduled plan");

    expectInvalid([] {
        static_cast<void>(GeneratedFramePlan::evenlySpaced(5));
    }, "a plan larger than the supported 5x capacity was accepted");
    expectInvalid([] {
        const std::array<float, 2> invalid{0.75F, 0.50F};
        static_cast<void>(GeneratedFramePlan::fromTimestamps(invalid));
    }, "unordered interpolation timestamps were accepted");
    expectInvalid([] {
        const std::array<float, 1> invalid{
            std::numeric_limits<float>::quiet_NaN()
        };
        static_cast<void>(GeneratedFramePlan::fromTimestamps(invalid));
    }, "a non-finite interpolation timestamp was accepted");
    expectInvalid([&] {
        static_cast<void>(scheduleAdmittedGeneratedFrames(explicitPlan, 4));
    }, "admission larger than the requested plan was accepted");

    std::cout << "generated frame plan tests passed\n";
    return 0;
}
