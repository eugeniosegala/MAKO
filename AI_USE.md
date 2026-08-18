# AI use in MAKO

MAKO uses AI-assisted development openly and extensively. AI is part of the
engineering workflow, but it does not replace technical ownership, review, or
release accountability.

## What AI is used for

Coding agents help accelerate work such as:

- exploring implementation options and tracing existing code paths;
- drafting routine implementation and refactoring work;
- writing and extending automated tests;
- adding diagnostics and instrumentation;
- analysing logs, performance data, and regressions;
- maintaining documentation and release tooling; and
- coordinating focused investigations across development and test environments.

The amount and type of assistance varies by task. An agent may draft a routine
change or help investigate a difficult edge case. A percentage of "AI-written
code" would therefore say little about how the software was designed, reviewed,
or validated.

## Human ownership and judgement

The project maintainer remains responsible for:

- defining what each feature must achieve;
- setting architectural, compatibility, performance, and safety constraints;
- deciding which trade-offs are acceptable;
- reviewing changes, including C++ and Vulkan code;
- defining the evidence required for validation; and
- deciding whether a change is ready to release.

Agent output is treated as a proposed engineering contribution, not as proof
that an implementation is correct. It is reviewed in context and can be
reworked or rejected.

## Validation

MAKO changes are validated according to their risk and scope. The process can
include code review, automated tests, native and Flatpak builds, targeted
instrumentation, log analysis, performance measurements, regression checks,
and testing on real SteamOS hardware.

Graphics and frame-timing work also has to account for behaviour that is hard
to establish from source code alone: changing frame rates, GPU pressure,
overlays, hitches, swapchain recreation, game restarts, compositor behaviour,
and recovery after unstable presentation. AI can help collect and analyse this
evidence, but the evidence—not the agent's confidence—determines whether a
change is accepted.

Adaptive Frame Generation is a useful example. Agents helped accelerate the
implementation and investigation work, while the scheduling model, safety
limits, recovery behaviour, acceptable trade-offs, and release decisions
remained human-directed and evidence-driven.

## Agent workflow

Development may involve multiple coding agents rather than a single assistant.
Through event-driven automation and webhooks, focused tasks can run across real
devices and virtual test environments, trigger scenarios, collect logs and
performance metrics, and feed that evidence into the next investigation or
implementation step. This creates a continuous loop between implementation,
measurement, review, and validation.

The current toolset includes Claude Code and Codex alongside conventional
engineering, build, test, profiling, and source-control tools. The specific
tools may change; the requirements for review and validation do not.

## Accountability

AI tools do not make final architectural or release decisions for MAKO. The
maintainer is accountable for the code accepted into the repository and for the
claims made about it.

## Further reading

- [Event-Driven Development for AI Agents](https://eugeniosegala.dev/event-driven-development-for-ai-agents/)
