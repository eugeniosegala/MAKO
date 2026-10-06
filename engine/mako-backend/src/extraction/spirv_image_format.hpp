/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <vector>

namespace mako::backend::detail {

    void patchStorageImageFormat(
        std::vector<uint8_t>& data, uint32_t imageFormat
    );

}
