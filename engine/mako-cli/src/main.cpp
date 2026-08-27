/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "arguments.hpp"
#include "i18n.hpp"
#include "tools/benchmark.hpp"
#include "tools/debug.hpp"
#include "tools/quality.hpp"
#include "tools/validate.hpp"

#include <array>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <getopt.h> // NOLINT (IWYU)

using namespace mako::cli;

namespace {
    /// print usage information
    void usage(const std::string& prog) {
        std::cerr <<
R"(Validate, benchmark, debug, and inspect MAKO Renderer.

USAGE:
    )" << prog << R"( [GLOBAL OPTIONS] <COMMAND> [OPTIONS] [ARGS]

GLOBAL OPTIONS:
    --lang <LANG>                       Output language: en, pt-BR, pt-PT, es
    -h, --help                          Show this help

COMMANDS:
    validate    Validate a configuration file
    benchmark   Run a benchmark
    debug       Run mako on a set of images
    quality-regression
                Run a procedural LSFG image-quality regression
    spatial-quality-regression
                Run a procedural MAKO/LS1 spatial-quality regression
    spatial-profile
                Profile the production spatial graph with Vulkan GPU timestamps
    synchronization-validation-canary
                Record an intentional hazard to prove synchronization validation
    combined-quality-regression
                Run spatial scaling into LSFG on one procedural scene

SUBCOMMAND OPTIONS:

    validate
        -c, --config <PATH>             Optional path to the configuration file

    benchmark & debug
        -d, --dll <PATH>                Path to Lossless.dll
        -a, --allow-fp16                Allow FP16 acceleration
        -w, --width <INT>               Width of the input frames
        -h, --height <INT>              Height of the input frames
        -f, --flow <FLOAT>              Flow scale
        -m, --multiplier <INT>          Multiplier
        -p, --performance-mode          Use performance mode
        -g, --gpu <STRING>              GPU to use

    benchmark
        -t, --duration <SECONDS>        Benchmark duration in seconds

    debug
        <folder>                        Path to the debug frames

    quality-regression
        -d, --dll <PATH>                Path to Lossless.dll
        -a, --allow-fp16                Include FP16 acceleration in the test
        -g, --gpu <STRING>              GPU to use
        -o, --output <DIRECTORY>        Write generated/reference PPM artifacts
        -s, --scene <NAME>              Procedural scene name
        -t, --interpolation <FLOAT>     Generated timestamp between 0 and 1
        -f, --flow <FLOAT>              Flow scale from 0.25 to 1.0
        -p, --performance-mode          Use the lighter LSFG model

    spatial-quality-regression
        -d, --dll <PATH>                Path to Lossless.dll for LS1 methods
        -g, --gpu <STRING>              GPU to use
        -o, --output <DIRECTORY>        Write generated/reference PPM artifacts
        -c, --scene <NAME>              Procedural scene name
        -m, --method <NAME>             mako, ls1, or ls1-performance
        -f, --factor <FLOAT>            Scaling factor above 1.0 through 2.0
        -s, --sharpness <FLOAT>         Sharpness from 0.0 through 1.0
        -t, --scene-time <FLOAT>        Scene time from 0.0 through 1.0
        -w, --width <INT>               Exact presentation width
        -h, --height <INT>              Exact presentation height

    spatial-profile
        -d, --dll <PATH>                Path to Lossless.dll for LS1 methods
        -g, --gpu <STRING>              GPU to use
        -m, --method <NAME>             mako, ls1, or ls1-performance
        -w, --width <INT>               Presentation width
        -h, --height <INT>              Presentation height
        -f, --factor <FLOAT>            Scaling factor above 1.0 through 2.0
        -s, --sharpness <FLOAT>         Sharpness from 0.0 through 1.0
        -u, --warmup <INT>              Untimed warm-up graph iterations
        -n, --samples <INT>             Timestamped graph iterations
        -x, --frame-generation-handoff Include the full-resolution FG source copy

    synchronization-validation-canary
        -g, --gpu <STRING>              GPU to use

    combined-quality-regression
        -d, --dll <PATH>                Path to Lossless.dll
        -a, --allow-fp16                Include FP16 LSFG acceleration
        -g, --gpu <STRING>              GPU to use
        -o, --output <DIRECTORY>        Write generated/reference PPM artifacts
        -c, --scene <NAME>              Procedural scene name
        -m, --method <NAME>             mako, ls1, or ls1-performance
        -f, --factor <FLOAT>            Scaling factor above 1.0 through 2.0
        -s, --sharpness <FLOAT>         Sharpness from 0.0 through 1.0
        -t, --interpolation <FLOAT>     Generated timestamp between 0 and 1
        -w, --flow <FLOAT>              Flow scale from 0.25 to 1.0
        -p, --performance-mode          Use the lighter LSFG model
            --width <INT>               Exact presentation width
            --height <INT>              Exact presentation height
)" << '\n';
    }

    /// parse the validate command options
    [[noreturn]] void on_validate(int argc, char** argv,
            const i18n::Language language, const std::string& program) {
        validate::Options opts{};

        const std::array<option, 3> GETOPT {{
            { "config", required_argument, nullptr, 'c' },
            { nullptr,        no_argument, nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(argc, argv, "c:", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'c':
                    opts.config.emplace(optarg);
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }

        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }

        std::exit(validate::run(opts, language));
    }

    /// parse the benchmark command options
    [[noreturn]] void on_benchmark(int argc, char** argv,
            const i18n::Language language, const std::string& program) {
        benchmark::Options opts{};

        const std::array<option, 10> GETOPT {{
            { "dll",              required_argument, nullptr, 'd' },
            { "allow-fp16",       no_argument,       nullptr, 'a' },
            { "width",            required_argument, nullptr, 'w' },
            { "height",           required_argument, nullptr, 'h' },
            { "flow",             required_argument, nullptr, 'f' },
            { "multiplier",       required_argument, nullptr, 'm' },
            { "performance-mode",       no_argument, nullptr, 'p' },
            { "gpu",              required_argument, nullptr, 'g' },
            { "duration",         required_argument, nullptr, 't' },
            { nullptr,                  no_argument, nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(argc, argv, "d:aw:h:f:m:pg:t:", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'd':
                    opts.dll.emplace(optarg);
                    break;
                case 'a':
                    opts.allow_fp16 = true;
                    break;
                case 'w':
                    opts.width = std::stoi(optarg);
                    break;
                case 'h':
                    opts.height = std::stoi(optarg);
                    break;
                case 'f':
                    opts.flow = std::stof(optarg);
                    break;
                case 'm':
                    opts.multiplier = std::stoi(optarg);
                    break;
                case 'p':
                    opts.performance_mode = true;
                    break;
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case 't':
                    opts.duration = std::stoi(optarg);
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }

        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }

        std::exit(benchmark::run(opts, language));
    }

    /// parse the debug command options
    [[noreturn]] void on_debug(int argc, char** argv,
            const i18n::Language language, const std::string& program) {
        debug::Options opts{};

        const std::array<option, 9> GETOPT {{
            { "dll",              required_argument, nullptr, 'd' },
            { "allow-fp16",       no_argument,       nullptr, 'a' },
            { "width",            required_argument, nullptr, 'w' },
            { "height",           required_argument, nullptr, 'h' },
            { "flow",             required_argument, nullptr, 'f' },
            { "multiplier",       required_argument, nullptr, 'm' },
            { "performance-mode",       no_argument, nullptr, 'p' },
            { "gpu",              required_argument, nullptr, 'g' },
            { nullptr,                  no_argument, nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(argc, argv, "d:aw:h:f:m:pg:", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'd':
                    opts.dll.emplace(optarg);
                    break;
                case 'a':
                    opts.allow_fp16 = true;
                    break;
                case 'w':
                    opts.width = std::stoi(optarg);
                    break;
                case 'h':
                    opts.height = std::stoi(optarg);
                    break;
                case 'f':
                    opts.flow = std::stof(optarg);
                    break;
                case 'm':
                    opts.multiplier = std::stoi(optarg);
                    break;
                case 'p':
                    opts.performance_mode = true;
                    break;
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }

        if ((optind + 1) != argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }

        opts.path = argv[optind];

        std::exit(debug::run(opts, language));
    }

    /// parse the quality-regression command options
    [[noreturn]] void on_quality_regression(int argc, char** argv,
            const std::string& program) {
        quality::Options opts{};
        const std::array<option, 9> GETOPT {{
            { "dll",              required_argument, nullptr, 'd' },
            { "allow-fp16",       no_argument,       nullptr, 'a' },
            { "gpu",              required_argument, nullptr, 'g' },
            { "output",           required_argument, nullptr, 'o' },
            { "scene",            required_argument, nullptr, 's' },
            { "interpolation",    required_argument, nullptr, 't' },
            { "flow",             required_argument, nullptr, 'f' },
            { "performance-mode", no_argument,       nullptr, 'p' },
            { nullptr,             no_argument,       nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(
                argc, argv, "d:ag:o:s:t:f:p", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'd':
                    opts.dll.emplace(optarg);
                    break;
                case 'a':
                    opts.allow_fp16 = true;
                    break;
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case 'o':
                    opts.output.emplace(optarg);
                    break;
                case 's':
                    opts.scene = optarg;
                    break;
                case 't':
                    opts.interpolation = std::stof(optarg);
                    break;
                case 'f':
                    opts.flow_scale = std::stof(optarg);
                    break;
                case 'p':
                    opts.performance_mode = true;
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }
        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }
        std::exit(quality::run(opts));
    }

    /// parse the spatial-quality-regression command options
    [[noreturn]] void on_spatial_quality_regression(int argc, char** argv,
            const std::string& program) {
        quality::SpatialOptions opts{};
        const std::array<option, 11> GETOPT {{
            { "dll",        required_argument, nullptr, 'd' },
            { "gpu",        required_argument, nullptr, 'g' },
            { "output",     required_argument, nullptr, 'o' },
            { "scene",      required_argument, nullptr, 'c' },
            { "method",     required_argument, nullptr, 'm' },
            { "factor",     required_argument, nullptr, 'f' },
            { "sharpness",  required_argument, nullptr, 's' },
            { "scene-time", required_argument, nullptr, 't' },
            { "width",      required_argument, nullptr, 'w' },
            { "height",     required_argument, nullptr, 'h' },
            { nullptr,       no_argument,       nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(
                argc, argv, "d:g:o:c:m:f:s:t:w:h:", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'd':
                    opts.dll.emplace(optarg);
                    break;
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case 'o':
                    opts.output.emplace(optarg);
                    break;
                case 'c':
                    opts.scene = optarg;
                    break;
                case 'm':
                    opts.method = optarg;
                    break;
                case 'f':
                    opts.scaling_factor = std::stof(optarg);
                    break;
                case 's':
                    opts.sharpness = std::stof(optarg);
                    break;
                case 't':
                    opts.scene_time = std::stof(optarg);
                    break;
                case 'w':
                    opts.width = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case 'h':
                    opts.height = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }
        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }
        std::exit(quality::runSpatial(opts));
    }

    /// Parse the spatial-profile command options.
    [[noreturn]] void on_spatial_profile(int argc, char** argv,
            const std::string& program) {
        quality::SpatialProfileOptions opts{};
        const std::array<option, 11> GETOPT {{
            { "dll",                      required_argument, nullptr, 'd' },
            { "gpu",                      required_argument, nullptr, 'g' },
            { "method",                   required_argument, nullptr, 'm' },
            { "width",                    required_argument, nullptr, 'w' },
            { "height",                   required_argument, nullptr, 'h' },
            { "factor",                   required_argument, nullptr, 'f' },
            { "sharpness",                required_argument, nullptr, 's' },
            { "warmup",                   required_argument, nullptr, 'u' },
            { "samples",                  required_argument, nullptr, 'n' },
            { "frame-generation-handoff", no_argument,       nullptr, 'x' },
            { nullptr,                       no_argument,       nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(
                argc, argv, "d:g:m:w:h:f:s:u:n:x", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'd':
                    opts.dll.emplace(optarg);
                    break;
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case 'm':
                    opts.method = optarg;
                    break;
                case 'w':
                    opts.width = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case 'h':
                    opts.height = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case 'f':
                    opts.scaling_factor = std::stof(optarg);
                    break;
                case 's':
                    opts.sharpness = std::stof(optarg);
                    break;
                case 'u':
                    opts.warmup_iterations = static_cast<uint32_t>(
                        std::stoul(optarg)
                    );
                    break;
                case 'n':
                    opts.samples = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case 'x':
                    opts.frame_generation_handoff = true;
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }
        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }
        std::exit(quality::runSpatialProfile(opts));
    }

    /// Parse the synchronization-validation-canary command options.
    [[noreturn]] void on_synchronization_validation_canary(
            int argc, char** argv, const std::string& program) {
        quality::SynchronizationCanaryOptions opts{};
        const std::array<option, 2> GETOPT {{
            { "gpu", required_argument, nullptr, 'g' },
            { nullptr,   no_argument, nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(
                argc, argv, "g:", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }
        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }
        std::exit(quality::runSynchronizationCanary(opts));
    }

    /// parse the combined-quality-regression command options
    [[noreturn]] void on_combined_quality_regression(int argc, char** argv,
            const std::string& program) {
        quality::CombinedOptions opts{};
        const std::array<option, 14> GETOPT {{
            { "dll",              required_argument, nullptr, 'd' },
            { "allow-fp16",       no_argument,       nullptr, 'a' },
            { "gpu",              required_argument, nullptr, 'g' },
            { "output",           required_argument, nullptr, 'o' },
            { "scene",            required_argument, nullptr, 'c' },
            { "method",           required_argument, nullptr, 'm' },
            { "factor",           required_argument, nullptr, 'f' },
            { "sharpness",        required_argument, nullptr, 's' },
            { "interpolation",    required_argument, nullptr, 't' },
            { "flow",             required_argument, nullptr, 'w' },
            { "performance-mode", no_argument,       nullptr, 'p' },
            { "width",             required_argument, nullptr, 1000 },
            { "height",            required_argument, nullptr, 1001 },
            { nullptr,             no_argument,       nullptr,  0  }
        }};

        int c{0};
        while ((c = getopt_long(
                argc, argv, "d:ag:o:c:m:f:s:t:w:p", GETOPT.data(), nullptr)) != -1) {
            switch (c) {
                case 'd':
                    opts.dll.emplace(optarg);
                    break;
                case 'a':
                    opts.allow_fp16 = true;
                    break;
                case 'g':
                    opts.gpu.emplace(optarg);
                    break;
                case 'o':
                    opts.output.emplace(optarg);
                    break;
                case 'c':
                    opts.scene = optarg;
                    break;
                case 'm':
                    opts.method = optarg;
                    break;
                case 'f':
                    opts.scaling_factor = std::stof(optarg);
                    break;
                case 's':
                    opts.sharpness = std::stof(optarg);
                    break;
                case 't':
                    opts.interpolation = std::stof(optarg);
                    break;
                case 'w':
                    opts.flow_scale = std::stof(optarg);
                    break;
                case 'p':
                    opts.performance_mode = true;
                    break;
                case 1000:
                    opts.width = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case 1001:
                    opts.height = static_cast<uint32_t>(std::stoul(optarg));
                    break;
                case '?':
                default:
                    usage(program);
                    std::exit(EXIT_FAILURE);
            }
        }
        if (optind < argc) {
            usage(program);
            std::exit(EXIT_FAILURE);
        }
        std::exit(quality::runCombined(opts));
    }
}

int main(int argc, char** argv) {
    const std::string program{argv[0]};
    std::vector<std::string_view> arguments;
    if (argc > 1)
        arguments.reserve(static_cast<size_t>(argc - 1));
    for (int index = 1; index < argc; ++index)
        arguments.emplace_back(argv[index]);

    const GlobalArgumentResult parsed = parse_global_arguments(
        std::span<const std::string_view>{arguments}
    );
    if (const auto* error = std::get_if<std::string>(&parsed)) {
        std::cerr << "mako-cli: " << *error << "\n\n";
        usage(program);
        return EXIT_FAILURE;
    }
    const GlobalArguments global = std::get<GlobalArguments>(parsed);
    if (global.show_help) {
        usage(program);
        return EXIT_SUCCESS;
    }

    const int command_offset = static_cast<int>(global.command_index) + 1;
    const int command_argc = argc - command_offset;
    char** command_argv = argv + command_offset;
    const std::string command{command_argv[0]};

    if (command == "validate")
        on_validate(command_argc, command_argv, global.language, program);
    else if (command == "benchmark")
        on_benchmark(command_argc, command_argv, global.language, program);
    else if (command == "debug")
        on_debug(command_argc, command_argv, global.language, program);
    else if (command == "quality-regression")
        on_quality_regression(command_argc, command_argv, program);
    else if (command == "spatial-quality-regression")
        on_spatial_quality_regression(command_argc, command_argv, program);
    else if (command == "spatial-profile")
        on_spatial_profile(command_argc, command_argv, program);
    else if (command == "synchronization-validation-canary")
        on_synchronization_validation_canary(
            command_argc, command_argv, program
        );
    else if (command == "combined-quality-regression")
        on_combined_quality_regression(command_argc, command_argv, program);

    usage(program);
    return EXIT_FAILURE;
}
