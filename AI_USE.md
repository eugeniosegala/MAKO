# AI use in MAKO

MAKO uses AI-assisted development openly and extensively. AI is part of the engineering workflow, but it does not replace technical ownership, review, or release accountability.

## What AI is used for

Coding agents help accelerate work such as:

- exploring implementation options and tracing existing code paths;
- drafting routine implementation and refactoring work;
- writing and extending automated tests;
- adding diagnostics and instrumentation;
- analysing logs, performance data, and regressions;
- maintaining documentation and release tooling; and
- coordinating focused investigations across development and test environments.

The amount and type of assistance varies by task. An agent may draft a routine change or help investigate a difficult edge case. A percentage of "AI-written code" would therefore say little about how the software was designed, reviewed, or validated.

## Human ownership and judgement

The project maintainer remains responsible for:

- defining what each feature must achieve;
- setting architectural, compatibility, performance, and safety constraints;
- deciding which trade-offs are acceptable;
- reviewing changes, including C++ and Vulkan code;
- defining the evidence required for validation; and
- deciding whether a change is ready to release.

Agent output is a proposed contribution, not proof of correctness. The maintainer reviews it in context and may rework or reject it.

## Validation

MAKO changes are validated according to their risk and scope. The process can include code review, automated tests, native and Flatpak builds, targeted instrumentation, log analysis, performance measurements, regression checks, and testing on real SteamOS hardware.

Graphics and frame-timing work must account for changing frame rates, GPU pressure, overlays, hitches, swapchain recreation, compositor behavior, and recovery after unstable presentation. AI can help collect and analyse this evidence, but evidence rather than agent confidence determines acceptance.

## Agent workflow

Development may use multiple focused agents and event-driven automation across local, virtual, and real-device environments. Their results feed the same implementation, measurement, review, and validation loop.

Repository agents follow [the repository guide](AGENTS.md) for directory placement, source-of-truth ownership, compatibility boundaries, generated files, and mutation limits, then use [the testing guide](TESTING.md) to select evidence in proportion to the affected boundary. They inspect the current worktree before editing, preserve unrelated changes, extend an existing owner instead of creating parallel constants or serialization paths, and report any hardware or runtime matrix that was not exercised.

Local implementation, refactoring, testing, or packaging does not by itself authorize a branch change, commit, push, deployment, tag, or release. Those mutations remain explicit maintainer decisions, and the release scripts enforce a clean, reviewed, hardware-validated path from Renderer publication through the pinned MAKO Decky package.

The current toolset includes Claude Code and Codex alongside conventional engineering, build, test, profiling, and source-control tools. Tools may change; human accountability and evidence requirements do not.

## Further reading

- [MAKO repository guide for coding agents](AGENTS.md)
- [Testing MAKO](TESTING.md)
- [How to release MAKO](HOW_TO_RELEASE.md)
- [Event-Driven Development for AI Agents](https://eugeniosegala.dev/event-driven-development-for-ai-agents/)
- [Bare-Metal Development for AI Agents](https://eugeniosegala.dev/bare-metal-development-for-ai-agents/)
