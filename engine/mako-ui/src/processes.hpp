/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace mako::ui {

    struct ProcessInfo {
        int pid;
        QString name;
        QString cmdline;
        int gpuUsage; // percentage, -1 if unknown
    };

    /// detect GPU vendor: "nvidia", "amd", "intel", or "unknown"
    QString detectGpuVendor();

    /// get running processes with GPU usage info, sorted by GPU usage desc
    QList<ProcessInfo> getRunningProcesses();

}
