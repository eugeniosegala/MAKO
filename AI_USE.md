# AI use in MAKO

MAKO uses AI-assisted development openly. AI can accelerate engineering work, but it does not replace technical ownership, review, or release accountability.

## What AI is used for

Coding agents may help with:

- tracing code and exploring implementation options;
- drafting focused changes and refactors;
- writing tests and diagnostics;
- analysing logs, performance data, and regressions; and
- maintaining documentation and tooling.

The amount of assistance varies by task, so a percentage of "AI-written code" would say little about how the software was designed, reviewed, or validated.

## Human ownership and judgement

The project maintainer remains responsible for:

- defining what each feature must achieve;
- setting architectural, compatibility, performance, and safety constraints;
- deciding which trade-offs are acceptable;
- reviewing changes, including C++ and Vulkan code;
- defining the evidence required for validation; and
- deciding whether a change is ready to release.

Agent output is a proposed contribution, not proof of correctness. The maintainer reviews it in context and may change or reject it.

## Validation

MAKO validates changes according to their risk and scope. Evidence can include review, automated tests, native and Flatpak builds, instrumentation, performance measurements, regression checks, and real SteamOS hardware.

Graphics and frame-timing work must also account for changing frame rates, GPU pressure, overlays, hitches, swapchain recreation, compositor behavior, and recovery. Evidence rather than agent confidence determines acceptance.

## Agent workflow

Repository agents follow [AGENTS.md](AGENTS.md) for ownership, generated files, compatibility, and mutation limits, then use [TESTING.md](TESTING.md) to select proportionate evidence. They preserve unrelated work and report any hardware or runtime coverage not exercised.

Local implementation, testing, or packaging does not authorize a branch change, commit, push, deployment, tag, or release. Those actions require an explicit maintainer decision. The separate release process in [HOW_TO_RELEASE.md](HOW_TO_RELEASE.md) requires clean source, an explicit hardware-evidence decision, ordered publication, and final public-package verification.

## Further reading

- [MAKO repository guide for coding agents](AGENTS.md)
- [Testing MAKO](TESTING.md)
- [How to release MAKO](HOW_TO_RELEASE.md)
- [Event-Driven Development for AI Agents](https://eugeniosegala.dev/event-driven-development-for-ai-agents/)
- [Bare-Metal Development for AI Agents](https://eugeniosegala.dev/bare-metal-development-for-ai-agents/)
