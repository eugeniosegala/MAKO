/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "tools/benchmark.hpp"
#include "tools/debug.hpp"
#include "tools/validate.hpp"
#include "translations.hpp"

#include <array>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <getopt.h> // NOLINT (IWYU)
#include <bits/getopt_core.h>
#include <bits/getopt_ext.h>

using namespace mako::cli;

namespace {
    void usage(const std::string& prog) {
        std::cerr <<
R"(Validate, benchmark, and debug mako.

USAGE:
    )" << prog << R"( [GLOBAL OPTIONS] <COMMAND> [OPTIONS] [ARGS]

GLOBAL OPTIONS:
    --lang <LANG>               Language: en, pt-BR, pt-PT, es (default: en)

COMMANDS:
    validate    Validate a configuration file
    benchmark   Run a benchmark
    debug       Run mako on a set of images

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
        <folder>                        Path to the debug frames)" << '\n';
    }

    [[noreturn]] void on_validate(int argc, char** argv, i18n::Lang lang) {
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
                    usage(*argv);
                    std::exit(EXIT_FAILURE);
            }
        }

        if (optind < argc) {
            usage(*argv);
            std::exit(EXIT_FAILURE);
        }

        std::exit(validate::run(opts, lang));
    }

    [[noreturn]] void on_benchmark(int argc, char** argv, i18n::Lang lang) {
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
                    usage(*argv);
                    std::exit(EXIT_FAILURE);
            }
        }

        if (optind < argc) {
            usage(*argv);
            std::exit(EXIT_FAILURE);
        }

        std::exit(benchmark::run(opts, lang));
    }

    [[noreturn]] void on_debug(int argc, char** argv, i18n::Lang lang) {
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
                    usage(*argv);
                    std::exit(EXIT_FAILURE);
            }
        }

        if ((optind + 1) != argc) {
            usage(*argv);
            std::exit(EXIT_FAILURE);
        }

        opts.path = argv[optind];

        std::exit(debug::run(opts, lang));
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(*argv);
        return EXIT_FAILURE;
    }

    // parse global --lang option before subcommand
    auto lang = i18n::Lang::En;
    int subargc = argc - 1;
    char** subargv = argv + 1;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--lang" && (i + 1) < argc) {
            lang = i18n::parseLang(argv[i + 1]);
            // shift arguments: remove --lang and its value
            subargc = argc - 3;
            subargv = argv + 1;
            // build new subargv excluding --lang and its value
            static char* filtered[64];
            int fi = 0;
            for (int j = 1; j < argc; j++) {
                if (std::string(argv[j]) == "--lang") {
                    j++; // skip value
                    continue;
                }
                filtered[fi++] = argv[j];
            }
            subargc = fi;
            subargv = filtered;
            break;
        }
    }

    const std::string command{subargv[0]};
    if (command == "validate")
        on_validate(subargc, subargv, lang);
    else if (command == "benchmark")
        on_benchmark(subargc, subargv, lang);
    else if (command == "debug")
        on_debug(subargc, subargv, lang);

    usage(*argv);
    return EXIT_FAILURE;
}
