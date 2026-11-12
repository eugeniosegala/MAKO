/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "context.hpp"
#include "mako-common/helpers/file_descriptors.hpp"
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace mako::backend {
struct HostWatch;
class MakoFG {
public:
    MakoFG(const DevicePicker&, const std::filesystem::path&, bool, const std::optional<DriverIdentity>& = std::nullopt);
    ~MakoFG();
    MakoFG(const MakoFG&) = delete;
    MakoFG& operator=(const MakoFG&) = delete;
    bool supportsPackedHdr10();
    std::unique_ptr<ContextImpl> open(ls::FileDescriptorScope&, ls::FileDescriptorScope&, ls::FileDescriptorScope&, uint32_t, uint32_t, FrameEncoding, float,int);
    // Replaces the program a lost context ran in, a bounded number of times per process. False once that limit is reached.
    bool restart(const ContextImpl&);
private:
    void connect();
    bool replaceHost();
    void ensureHost();
    // The socket that starts contexts in mako-fg-host; the host ends when this closes.
    int control{-1};
    mutable std::mutex controlLock;
    std::unique_ptr<HostWatch> watch;
    std::vector<std::string> environment;
    // The game's own linker and library search path, when the program has to start through them; empty otherwise.
    std::vector<std::string> launcher;
    std::optional<std::filesystem::path> package;
    std::string label;
    uint32_t hostGeneration{},restarts{};
    bool restartLimitReported{};
    int deviceIndex{-1};
    bool halfStorage{};
};
}
