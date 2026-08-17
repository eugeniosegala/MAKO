/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
    constexpr double pqM1 = 2610.0 / 16384.0;
    constexpr double pqM2 = 2523.0 / 32.0;
    constexpr double pqC1 = 3424.0 / 4096.0;
    constexpr double pqC2 = 2413.0 / 128.0;
    constexpr double pqC3 = 2392.0 / 128.0;

    double linearToPq(const double linear) {
        const double p = std::pow(std::clamp(linear, 0.0, 1.0), pqM1);
        return std::pow((pqC1 + pqC2 * p) / (1.0 + pqC3 * p), pqM2);
    }

    double pqToLinear(const double encoded) {
        const double p = std::pow(std::clamp(encoded, 0.0, 1.0), 1.0 / pqM2);
        const double numerator = std::max(p - pqC1, 0.0);
        const double denominator = std::max(pqC2 - pqC3 * p, 1e-12);
        return std::pow(numerator / denominator, 1.0 / pqM1);
    }

    void expectNear(const double actual, const double expected,
            const double tolerance, const std::string_view message) {
        if (std::abs(actual - expected) <= tolerance)
            return;
        std::cerr << "FAIL: " << message << "; expected=" << expected
                  << "; actual=" << actual << '\n';
        std::exit(1);
    }
}

int main() {
    // SMPTE ST 2084 encodes 100 nits as 0.01 of its 10,000-nit range.
    const double pq100Nits = linearToPq(0.01);
    expectNear(pq100Nits, 0.5080784215, 1e-9,
        "100-nit ST 2084 reference value");
    expectNear(pqToLinear(pq100Nits) * 125.0, 1.25, 1e-9,
        "100 nits should be 1.25 in 80-nit scRGB units");

    for (const double linear : std::array{0.0, 0.0001, 0.01, 0.1, 1.0})
        expectNear(pqToLinear(linearToPq(linear)), linear, 1e-9,
            "ST 2084 encode/decode round trip");

    constexpr std::array<double, 9> bt2020ToBt709{
         1.660491, -0.587641, -0.072850,
        -0.124550,  1.132900, -0.008349,
        -0.018151, -0.100579,  1.118730,
    };
    constexpr std::array<double, 9> bt709ToBt2020{
        0.627404, 0.329283, 0.043313,
        0.069097, 0.919540, 0.011362,
        0.016391, 0.088013, 0.895595,
    };

    // Every neutral value must remain neutral through both gamut matrices.
    for (size_t row = 0; row < 3; ++row) {
        double forwardSum{};
        double inverseSum{};
        for (size_t column = 0; column < 3; ++column) {
            forwardSum += bt2020ToBt709.at(row * 3 + column);
            inverseSum += bt709ToBt2020.at(row * 3 + column);
        }
        expectNear(forwardSum, 1.0, 2e-6, "BT.2020 to BT.709 neutral axis");
        expectNear(inverseSum, 1.0, 2e-6, "BT.709 to BT.2020 neutral axis");
    }

    // The rounded matrices must remain close inverses for saturated colours.
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            double product{};
            for (size_t inner = 0; inner < 3; ++inner) {
                product += bt2020ToBt709.at(row * 3 + inner) *
                    bt709ToBt2020.at(inner * 3 + column);
            }
            expectNear(product, row == column ? 1.0 : 0.0, 2e-6,
                "BT.2020/BT.709 gamut round trip");
        }
    }

    std::cout << "All HDR colour-math tests passed.\n";
    return 0;
}
