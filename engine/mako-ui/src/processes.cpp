/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "processes.hpp"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <filesystem>
#include <fstream>
#include <algorithm>

using namespace mako::ui;

QString mako::ui::detectGpuVendor() {
    const std::string drmPath = "/sys/class/drm";
    if (!std::filesystem::exists(drmPath))
        return "unknown";

    for (const auto& entry : std::filesystem::directory_iterator(drmPath)) {
        const auto& path = entry.path();
        auto filename = path.filename().string();
        if (filename.find("card") == std::string::npos)
            continue;

        auto vendorPath = path / "device" / "vendor";
        if (!std::filesystem::exists(vendorPath))
            continue;

        std::ifstream vendorFile(vendorPath);
        std::string vendor;
        std::getline(vendorFile, vendor);

        if (vendor == "0x10de") return "nvidia";
        if (vendor == "0x1002") return "amd";
        if (vendor == "0x8086") return "intel";
    }

    return "unknown";
}

namespace {
    struct RawProcess {
        int pid;
        QString name;
        QString cmdline;
    };

    QString readProcComm(int pid) {
        QFile file(QString("/proc/%1/comm").arg(pid));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return file.readAll().trimmed();
    }

    QString readProcCmdline(int pid) {
        QFile file(QString("/proc/%1/cmdline").arg(pid));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        QByteArray data = file.readAll();
        // cmdline is null-separated, replace with spaces
        data.replace('\0', ' ');
        return QString::fromLocal8Bit(data).trimmed();
    }

    bool isNumeric(const QString& s) {
        for (const auto& c : s)
            if (!c.isDigit()) return false;
        return !s.isEmpty();
    }

    QList<RawProcess> readAllProcesses() {
        QList<RawProcess> processes;
        QDir procDir("/proc");
        const auto entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const auto& entry : entries) {
            if (!isNumeric(entry)) continue;
            int pid = entry.toInt();
            if (pid <= 0) continue;

            // skip kernel threads (no cmdline)
            QFile cmdlineFile(QString("/proc/%1/cmdline").arg(pid));
            if (!cmdlineFile.open(QIODevice::ReadOnly))
                continue;
            QByteArray cmdData = cmdlineFile.readAll();
            if (cmdData.isEmpty()) continue;

            QString name = readProcComm(pid);
            QString cmdline = QString::fromLocal8Bit(cmdData).replace('\0', ' ').trimmed();

            // skip our own process
            if (name == "mako-ui" || name == "mako-cli") continue;
            // skip system processes
            if (name.isEmpty() || name.startsWith("[")) continue;

            processes.append({pid, name, cmdline});
        }

        return processes;
    }

    // parse nvidia-smi output to get per-process GPU usage
    QMap<int, int> getNvidiaGpuUsage() {
        QMap<int, int> usage;

        QProcess proc;
        proc.start("nvidia-smi", {
            "--query-compute-apps=pid,used_gpu_memory",
            "--format=csv,noheader,nounits"
        });
        proc.waitForFinished(3000);

        if (proc.exitCode() != 0) return usage;

        const auto lines = QString::fromLocal8Bit(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            const auto parts = line.split(',');
            if (parts.size() < 2) continue;
            int pid = parts[0].trimmed().toInt();
            // we don't have per-process % from nvidia-smi query,
            // but we know the process is using GPU
            if (pid > 0) usage[pid] = -2; // marker: known GPU process
        }

        // try to get overall GPU utilization
        QProcess utilProc;
        utilProc.start("nvidia-smi", {
            "--query-gpu=utilization.gpu",
            "--format=csv,noheader,nounits"
        });
        utilProc.waitForFinished(3000);

        if (utilProc.exitCode() == 0) {
            int gpuUtil = QString::fromLocal8Bit(utilProc.readAllStandardOutput()).trimmed().toInt();
            // assign the overall utilization to GPU processes
            for (auto it = usage.begin(); it != usage.end(); ++it) {
                if (it.value() == -2) it.value() = gpuUtil;
            }
        }

        return usage;
    }

    // parse rocm-smi output to get per-process GPU usage
    QMap<int, int> getAmdGpuUsage() {
        QMap<int, int> usage;

        QProcess proc;
        proc.start("rocm-smi", {"--showprocessuse", "--json"});
        proc.waitForFinished(3000);

        if (proc.exitCode() != 0) {
            // fallback: try rocm-smi without json
            QProcess fallback;
            fallback.start("rocm-smi", {"--showprocessuse"});
            fallback.waitForFinished(3000);
            if (fallback.exitCode() != 0) return usage;

            const auto output = QString::fromLocal8Bit(fallback.readAllStandardOutput());
            // parse lines like: "PID 12345, 45% GPU"
            QRegularExpression re(R"(PID\s+(\d+),?\s+(\d+)%?\s*GPU)");
            QRegularExpressionMatchIterator it = re.globalMatch(output);
            while (it.hasNext()) {
                auto match = it.next();
                int pid = match.captured(1).toInt();
                int util = match.captured(2).toInt();
                if (pid > 0) usage[pid] = util;
            }
            return usage;
        }

        const auto json = QJsonDocument::fromJson(proc.readAllStandardOutput()).object();
        // try to find process info in JSON
        const auto processList = json["program-list"].toArray();
        for (const auto& procEntry : processList) {
            auto obj = procEntry.toObject();
            int pid = obj["pid"].toInt();
            int gpuUse = obj["gpu-use"].toInt();
            if (pid > 0) usage[pid] = gpuUse;
        }

        return usage;
    }

    QMap<int, int> getGpuUsageMap() {
        const auto vendor = detectGpuVendor();
        if (vendor == "nvidia") return getNvidiaGpuUsage();
        if (vendor == "amd") return getAmdGpuUsage();
        return {};
    }
}

QList<ProcessInfo> mako::ui::getRunningProcesses() {
    auto rawProcesses = readAllProcesses();
    const auto gpuUsage = getGpuUsageMap();

    QList<ProcessInfo> result;
    for (const auto& rp : rawProcesses) {
        ProcessInfo info;
        info.pid = rp.pid;
        info.name = rp.name;
        info.cmdline = rp.cmdline;
        info.gpuUsage = gpuUsage.value(rp.pid, -1);
        result.append(info);
    }

    // sort: GPU processes first (by usage desc), then unknown, then alphabetical
    std::sort(result.begin(), result.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        if (a.gpuUsage >= 0 && b.gpuUsage < 0) return true;
        if (a.gpuUsage < 0 && b.gpuUsage >= 0) return false;
        if (a.gpuUsage >= 0 && b.gpuUsage >= 0) return a.gpuUsage > b.gpuUsage;
        return a.name.toLower() < b.name.toLower();
    });

    return result;
}
