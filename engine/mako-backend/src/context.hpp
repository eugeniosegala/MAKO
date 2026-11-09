/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "mako-backend/mako.hpp"
namespace mako::backend {
// Internal runtime boundary; the public handle remains opaque.
class ContextImpl {
public:
    virtual ~ContextImpl() = default;
    virtual void scheduleFrames() = 0;
    virtual void scheduleFrames(std::span<const float>) = 0;
    virtual void scheduleFrameHistory() = 0;
    virtual bool waitForIdle(uint64_t) const = 0;
    virtual void enableFrameProfiling() = 0;
    virtual FrameProfile readFrameProfile() const = 0;
    // Sets what releases application work waiting on this context, for a separate program that stops. A context
    // that already lost its program runs it at once; an empty handler withdraws an earlier one.
    virtual void setLossHandler(std::function<void()>) {}
    // Whether this context's separate program stopped, so its work will never finish.
    [[nodiscard]] virtual bool lost() const { return false; }
    // Which start of that program this context belongs to.
    [[nodiscard]] virtual uint32_t generation() const { return 0; }
};
}
