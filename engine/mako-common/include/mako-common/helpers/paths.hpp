/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <filesystem>
#include <optional>

namespace ls {

    /// find the location of the Lossless.dll
    /// @returns the path to the DLL
    /// @throws ls::error if the DLL could not be found
    std::filesystem::path findShaderDll();

    /// Resolve the locally installed MAKO FG package, or its explicit override.
    /// Loading validates availability; choosing LSFG never reads this package.
    std::filesystem::path makoFgPackagePath();
    /// CLI-only override. The layer uses the saved model choice explicitly.
    std::optional<std::filesystem::path> makoFgPackageOverride();

}
