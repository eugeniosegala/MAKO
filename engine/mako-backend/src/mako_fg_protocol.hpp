/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
// The socket messages between the Renderer and mako-fg-host, a separate program with its own declaration of this
// format. It is host-endian, and 32-bit Renderers talk to the same 64-bit program, so each message keeps one layout on
// both. Change hostProtocol with any change here; packaging compares it with the program.
#include <cstddef>
#include <cstdint>
namespace mako::backend::fg_host {
constexpr uint32_t hostProtocol=2;
constexpr size_t maxOutputs=32;
constexpr int hostDescriptor=3;
enum Op:uint32_t {Hello=1,Devices,Supports,Open,Schedule,Ready,EnableProfiling,ReadProfile};
struct alignas(8) Request {
    uint32_t op,count;uint64_t timeout;int32_t device,encoding,period;uint32_t half,width,height;
    float flow,times[maxOutputs];
};
struct Reply {int32_t failed;uint32_t first,second;char text[4084];};
struct GpuInfo {int32_t index;uint32_t vendor,id;char pci[32],name[256];uint8_t driverUuid[16],deviceUuid[16];};
struct alignas(8) ProfileData {
    double previousWait,submit,input,analysis,span,synthesis[maxOutputs],output[maxOutputs],generated[maxOutputs];
    uint32_t validBits,synthesisCount,outputCount,generatedCount;float period;
};
static_assert(sizeof(Request)==176&&sizeof(ProfileData)==832);
}
