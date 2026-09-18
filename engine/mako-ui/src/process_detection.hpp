/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/detection.hpp"

#include <QString>
#include <vector>

namespace mako::ui {

struct RunningGame {
    int pid{};
    QString name;
    QString executable;
};

// User-requested snapshots only; never inspect command lines or environments.
std::vector<RunningGame> detectRunningGames(
    const std::filesystem::path& procRoot, bool includeAllApplications);

// Recheck identity before capture so an exited process or reused PID is inert.
std::optional<ls::Identification> runningGameIdentity(
    const RunningGame& game, const std::filesystem::path& procRoot);

}
