/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <vector>

namespace mako::backend::detail {

    void patchLs1StorageImageFormat(
        std::vector<uint8_t>& data, uint32_t imageFormat
    );

}
