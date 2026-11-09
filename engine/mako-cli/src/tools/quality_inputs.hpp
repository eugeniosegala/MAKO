/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "mako-common/quality/image_quality.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace mako::cli::quality {
    struct QualityInput {
        uint32_t width;
        uint32_t height;
        std::vector<uint8_t> rgba;
    };

    /// Read the canonical three-line P6 format written by the quality tool.
    /// Deliberately reject comments, alternate depths, padding and trailing data.
    inline QualityInput readQualityInput(const std::filesystem::path& path) {
        constexpr uint64_t maximumPixels = 16'000'000;
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) ||
                std::filesystem::file_size(path) > maximumPixels * 3 + 128)
            throw std::invalid_argument("quality input must be a bounded regular P6 file");
        std::ifstream input(path, std::ios::binary);
        std::string magic, extent, depth;
        if (!std::getline(input, magic) || !std::getline(input, extent) ||
                !std::getline(input, depth) || magic != "P6" || depth != "255" ||
                extent.size() > 64)
            throw std::invalid_argument("quality input requires canonical RGB8 P6 headers");
        uint64_t width = 0, height = 0;
        std::istringstream dimensions(extent);
        std::string extra;
        if (!(dimensions >> width >> height) || dimensions >> extra ||
                width < 2 || height < 2 || width > 16384 || height > 16384 ||
                width * height > maximumPixels)
            throw std::invalid_argument("quality input extent is invalid");
        const auto pixels = static_cast<size_t>(width * height);
        std::vector<uint8_t> rgb(pixels * 3);
        input.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (input.gcount() != static_cast<std::streamsize>(rgb.size()) ||
                input.get() != std::char_traits<char>::eof())
            throw std::invalid_argument("quality input payload does not match its extent");
        QualityInput result{static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            std::vector<uint8_t>(pixels * 4, 255)};
        for (size_t pixel = 0; pixel < pixels; ++pixel)
            for (size_t channel = 0; channel < 3; ++channel)
                result.rgba[pixel * 4 + channel] = rgb[pixel * 3 + channel];
        return result;
    }

    inline mako::quality::RegressionScene readQualityPair(const std::filesystem::path& directory) {
        auto previous = readQualityInput(directory / "previous.ppm");
        auto current = readQualityInput(directory / "current.ppm");
        auto reference = readQualityInput(directory / "reference.ppm");
        if (previous.width != current.width || previous.height != current.height ||
                previous.width != reference.width || previous.height != reference.height)
            throw std::invalid_argument("quality input pair extents differ");
        const auto pixels = static_cast<size_t>(previous.width) * previous.height;
        return {previous.width, previous.height, std::move(previous.rgba),
            std::move(current.rgba), std::move(reference.rgba),
            std::vector<uint8_t>(pixels, 1), std::vector<uint8_t>(pixels, 0)};
    }
}
