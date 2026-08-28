/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "helpers/image_prefix.hpp"
#include "helpers/timestamp_upload_cache.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    void require(const bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool approximatelyEqual(const float lhs, const float rhs) {
        return std::abs(lhs - rhs) < 0.000001F;
    }

    size_t applyTimestamps(std::vector<float>& cache,
            const std::vector<float>& timestamps) {
        size_t writes = 0;
        for (size_t i = 0; i < timestamps.size(); ++i) {
            mako::backend::uploadChangedTimestamp(
                cache, i, timestamps.at(i),
                [&writes](const float) { ++writes; }
            );
        }
        return writes;
    }

    void testStableAndChangingCounts() {
        std::vector<float> cache{0.25F, 0.5F, 0.75F};

        require(applyTimestamps(cache, {0.5F}) == 1,
            "3-output to 1-output transition did not upload once");
        require(applyTimestamps(cache, {0.5F}) == 0,
            "stable 1-output timestamp uploaded again");
        require(applyTimestamps(cache, {0.25F, 0.5F, 0.75F}) == 1,
            "1-output to 3-output transition uploaded the wrong count");
        require(applyTimestamps(cache, {1.0F / 3.0F, 2.0F / 3.0F}) == 2,
            "3-output to 2-output transition did not refresh both outputs");
        require(applyTimestamps(cache, {0.5F}) == 1,
            "2-output to 1-output transition did not refresh output zero");
    }

    void testFailedWriteDoesNotCommit() {
        std::vector<float> cache{0.5F};
        try {
            mako::backend::uploadChangedTimestamp(
                cache, 0, 0.25F,
                [](const float) { throw std::runtime_error("upload failed"); }
            );
            throw std::runtime_error("failed upload unexpectedly returned");
        } catch (const std::runtime_error& error) {
            require(std::string{error.what()} == "upload failed",
                "test caught the wrong upload exception");
        }

        require(approximatelyEqual(cache.at(0), 0.5F),
            "failed upload changed the cached timestamp");
        size_t writes = 0;
        require(mako::backend::uploadChangedTimestamp(
            cache, 0, 0.25F,
            [&writes](const float) { ++writes; }
        ), "retry after failed upload was skipped");
        require(writes == 1 && approximatelyEqual(cache.at(0), 0.25F),
            "successful retry did not commit the timestamp");
    }

    void testRequiredImagePrefixIsExactAndFailsClosed() {
        const std::vector<int> images{10, 20, 30, 40};
        const auto prefix = mako::backend::requiredPrefix(images, 2, "test");
        require(prefix.size() == 2 && prefix.front() == 10 && prefix.back() == 20,
            "required image prefix did not preserve the exact descriptor width");

        try {
            static_cast<void>(mako::backend::requiredPrefix(images, 5, "test"));
            throw std::runtime_error("oversized image prefix unexpectedly succeeded");
        } catch (const std::invalid_argument& error) {
            require(std::string{error.what()} == "test image set is too small",
                "required image prefix reported the wrong failure");
        }
    }
}

int main() {
    try {
        testStableAndChangingCounts();
        testFailedWriteDoesNotCommit();
        testRequiredImagePrefixIsExactAndFailsClosed();
    } catch (const std::exception& error) {
        std::cerr << "Backend hot-path test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
