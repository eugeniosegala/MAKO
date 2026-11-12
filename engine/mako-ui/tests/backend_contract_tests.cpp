/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "backend.hpp"
#include "utils.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaObject>
#include <QMetaProperty>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

// GPU discovery is unrelated to persistence and must not require a Vulkan host.
QStringList mako::ui::getAvailableGPUs() {
    return {QStringLiteral("Default")};
}

namespace {

void require(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void require_property(const char* name, const char* type_name,
        const bool writable, const bool constant) {
    const QMetaObject& meta_object = mako::ui::Backend::staticMetaObject;
    const int property_index = meta_object.indexOfProperty(name);
    require(property_index >= 0, "required Qt backend property is missing");

    const QMetaProperty property = meta_object.property(property_index);
    require(std::strcmp(property.typeName(), type_name) == 0,
        "Qt backend property has the wrong type");
    require(property.isReadable(), "Qt backend property is not readable");
    require(property.isWritable() == writable,
        "Qt backend property has the wrong writable contract");
    require(property.isConstant() == constant,
        "Qt backend property has the wrong constant contract");
    require(constant || property.hasNotifySignal(),
        "writable Qt backend property has no notification signal");
}

void test_installed_launcher_selection() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary launcher fixture directory failed");
    const QString packageBin = directory.filePath("arch package/bin");
    const QString pathBin = directory.filePath("other/bin");
    require(QDir().mkpath(packageBin) && QDir().mkpath(pathBin),
        "cannot create launcher fixture directories");
    const auto createLauncher = [](const QString& path) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "cannot create launcher fixture");
        require(file.write("#!/bin/sh\nexit 0\n") > 0,
            "cannot write launcher fixture");
        file.close();
        require(file.setPermissions(QFileDevice::ReadOwner |
                QFileDevice::WriteOwner | QFileDevice::ExeOwner),
            "cannot mark launcher fixture executable");
    };
    const QString packagedLauncher = packageBin + "/mako-launch";
    const QString pathLauncher = pathBin + "/mako-launch";
    createLauncher(packagedLauncher);
    createLauncher(pathLauncher);
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", pathBin.toUtf8());
    require(mako::ui::launcherCommandForUiDirectory(packageBin) ==
            "'" + packagedLauncher + "'",
        "the UI did not select its packaged launcher before PATH");
    require(mako::ui::launcherCommandForUiDirectory(directory.path()) ==
            "'" + pathLauncher + "'",
        "the UI did not resolve a launcher on PATH when no sibling exists");
    const QString nonFileBin = directory.filePath("directory launcher/bin");
    require(QDir().mkpath(nonFileBin + "/mako-launch"),
        "cannot create non-file launcher fixture");
    require(mako::ui::launcherCommandForUiDirectory(nonFileBin) ==
            "'" + pathLauncher + "'",
        "the UI accepted a directory as its installed launcher");
    qputenv("PATH", QByteArray{});
    require(mako::ui::launcherCommandForUiDirectory(directory.path()) ==
            QStringLiteral("~/.local/bin/mako-launch"),
        "the UI lost the user-local fallback when no launcher exists");
    if (previousPath.isNull()) qunsetenv("PATH");
    else qputenv("PATH", previousPath);
}

void test_installed_launcher_wine_environment() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary Wine launch fixture directory failed");
    const auto bin = directory.filePath("Qt user's package/bin");
    require(QDir().mkpath(bin), "cannot create Qt launch fixture directory");
    const auto launcher = bin + "/mako-launch";
    require(QFile::copy(QStringLiteral(MAKO_UI_LAUNCHER), launcher),
        "cannot stage the Renderer launcher for Qt");
    require(QFile::setPermissions(launcher, QFileDevice::ReadOwner |
            QFileDevice::WriteOwner | QFileDevice::ExeOwner),
        "cannot make the Renderer launcher executable");
    const auto command = mako::ui::launcherCommandForUiDirectory(bin) +
        QStringLiteral(" /bin/sh -c 'printf \"%s\" \"${WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK-unset}\"'");
    const auto check = [&](const std::optional<QString>& value,
            const bool disabled, const QString& expected) {
        // Isolate the user's launch settings, layer selection and display.
        QProcessEnvironment environment;
        environment.insert("PATH", "/usr/bin:/bin");
        environment.insert("HOME", directory.path());
        environment.insert("XDG_CONFIG_HOME", directory.filePath("config"));
        environment.insert("XDG_DATA_HOME", directory.filePath("data"));
        if (value)
            environment.insert("WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK", *value);
        if (disabled)
            environment.insert("DISABLE_MAKO", "1");
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start("/bin/bash", {"-c", command});
        require(process.waitForStarted(5000) && process.waitForFinished(5000),
            "Qt's Renderer launch command did not finish");
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            "Qt's Renderer launch command failed");
        require(process.readAllStandardOutput() == expected.toUtf8(),
            "Qt's Renderer launcher lost the Wine default or caller override");
    };
    check(std::nullopt, false, "1");
    check(QStringLiteral("0"), false, "0");
    check(QStringLiteral("1"), false, "1");
    check(QStringLiteral(""), false, "");
    check(std::nullopt, true, "unset");
    check(QStringLiteral("0"), true, "0");
}

void test_mangohud_profile_and_availability() {
    require_property("enable_mangohud", "bool", true, false);
    require_property("mangohud_available", "bool", false, false);
    require_property("mangohud_checking", "bool", false, false);
    require_property("mangohud_count_real_frames", "bool", true, false);
    QTemporaryDir directory;
    require(directory.isValid(), "MangoHud fixture directory failed");
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto previousShaders = qgetenv("MAKO_VKBASALT_SHADER_DIR");
    qputenv("MAKO_CONFIG", directory.filePath("conf.toml").toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launcher.conf").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("MAKO_VKBASALT_SHADER_DIR", QByteArray(MAKO_UI_VKBASALT_SHADER_SOURCE_DIR));
    ls::ConfigFile fixture;
    fixture.profiles().front().name = "Mango game";
    ls::GameConf other;
    other.name = "Other game";
    fixture.profiles().push_back(other);
    fixture.write(directory.filePath("conf.toml").toStdString());
    const auto helper = directory.filePath("launcher helper");
    const auto writeHelper = [&](const QByteArray& response) {
        QFile file(helper);
        require(file.open(QIODevice::WriteOnly), "cannot write MangoHud check fixture");
        file.write("#!/bin/sh\n[ \"$1\" = --check-mangohud ] || exit 1\nprintf '%s\\n' '");
        file.write(response);
        file.write("'\n");
        file.close();
        require(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
            "cannot make check fixture executable");
    };
    const auto wait = [](mako::ui::Backend& backend) {
        QElapsedTimer timer;
        timer.start();
        while (backend.mangoHudChecking() && timer.elapsed() < 3000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        QCoreApplication::processEvents();
        require(!backend.mangoHudChecking(), "MangoHud availability check did not finish");
    };
    writeHelper("{\"available\":true}");
    {
        mako::ui::Backend backend("/proc", {}, {}, helper);
        wait(backend);
        require(backend.mangoHudAvailable() && !backend.getEnableMangoHud(),
            "availability must not enable MangoHud automatically");
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltSharpnessUpdated(0.65F);
        backend.enableMangoHudUpdated(true);
        require(backend.getEnableMangoHud() && !backend.getEnableVkBasalt() &&
                backend.getLaunchOption().contains("MANGOHUD=1") &&
                !backend.getLaunchOption().contains("ENABLE_VKBASALT=1"),
            "MangoHud must replace Shaders in the copied launch option");
        backend.profileSelected(1);
        require(!backend.getEnableMangoHud(), "MangoHud leaked to another profile");
        backend.profileSelected(0);
        require(backend.getEnableMangoHud(), "MangoHud selection was not retained");
        require(!backend.getMangoHudCountRealFrames() &&
                !backend.getLaunchOption().contains("MAKO_MANGOHUD_REAL_FRAMES"),
            "MangoHud must count every output frame by default");
        backend.mangoHudCountRealFramesUpdated(true);
        require(backend.getLaunchOption().contains("MANGOHUD=1 MAKO_MANGOHUD_REAL_FRAMES=1"),
            "the real-frame count is missing from the copied launch option");
        backend.profileSelected(1);
        require(!backend.getMangoHudCountRealFrames(), "the MangoHud frame count leaked to another profile");
        backend.profileSelected(0);
        backend.enableVkBasaltUpdated(true);
        backend.enableVkBasaltUpdated(false);
        require(!backend.getEnableMangoHud(), "disabling Shaders resurrected an old MangoHud selection");
        backend.enableMangoHudUpdated(true);
        require(backend.getVkBasaltSharpness() == 0.65F, "switching external tools lost shader settings");
        require(backend.createProfile("Mango copy") && backend.getEnableMangoHud() &&
                backend.renameProfile("Mango renamed") && backend.getEnableMangoHud() &&
                backend.getMangoHudCountRealFrames(),
            "profile copy or rename discarded MangoHud");
        require(backend.getLaunchOption().contains("MAKO_PROFILE='Mango renamed'"),
            "renamed profile launch option is stale");
    }
    writeHelper("{\"available\":false}");
    {
        mako::ui::Backend backend("/proc", {}, {}, helper);
        wait(backend);
        require(!backend.mangoHudAvailable() && backend.getEnableMangoHud(),
            "an unavailable installation must preserve the saved profile choice");
        backend.enableMangoHudUpdated(false);
        require(!backend.getEnableMangoHud() && !backend.getLaunchOption().contains("MANGOHUD=1") &&
                !backend.getLaunchOption().contains("MAKO_MANGOHUD_REAL_FRAMES"),
            "an unavailable saved MangoHud selection could not be turned off");
        backend.enableMangoHudUpdated(true);
        require(!backend.getEnableMangoHud(), "Qt enabled an unavailable MangoHud installation");
        writeHelper("{\"available\":\"true\"}");
        backend.refreshMangoHud();
        wait(backend);
        require(!backend.mangoHudAvailable(), "malformed check response enabled MangoHud");
        writeHelper("{\"available\":true}");
        backend.refreshMangoHud();
        wait(backend);
        backend.enableMangoHudUpdated(true);
        require(backend.getEnableMangoHud(), "refresh did not discover the newly installed MangoHud");
        backend.profileSelected(0);
    }
    QFile sidecar(directory.filePath("profile-wrapper-settings.json"));
    require(sidecar.open(QIODevice::ReadOnly), "Qt did not save the shared profile sidecar");
    const auto profiles = QJsonDocument::fromJson(sidecar.readAll()).object().value("profiles").toObject();
    require(profiles.value("Mango game").toObject().value("external_vulkan_layer") == "mangohud" &&
            profiles.value("Mango renamed").toObject().value("external_vulkan_layer") == "mangohud" &&
            profiles.value("Other game").toObject().value("external_vulkan_layer") == "",
        "Qt did not preserve Decky's per-profile external-layer format");
    require(profiles.value("Mango game").toObject().value("mangohud_count_real_frames") == true &&
            profiles.value("Mango renamed").toObject().value("mangohud_count_real_frames") == true &&
            !profiles.value("Other game").toObject().contains("mangohud_count_real_frames"),
        "Qt did not store Decky's per-profile MangoHud frame-count format");
    for (const auto& entry : {std::pair{"MAKO_CONFIG", previousConfig},
            std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch},
            std::pair{"XDG_CONFIG_HOME", previousConfigHome},
            std::pair{"MAKO_VKBASALT_SHADER_DIR", previousShaders}}) {
        if (entry.second.isNull()) qunsetenv(entry.first); else qputenv(entry.first, entry.second);
    }
}

void test_scaling_properties() {
    require_property("hdr_quality", "QString", true, false);
    require_property("running_games", "QVariantList", false, false);
    require_property("scanning_games", "bool", false, false);
    require_property("capture_failed", "bool", false, false);
    static_assert(!ls::GameConfDefaults::scalingEnabled);
    static_assert(ls::GameConfDefaults::scalingMethod ==
        ls::ScalingMethod::Mako);
    static_assert(ls::GameConfDefaults::scalingFactor == 1.5F);
    static_assert(!ls::GameConfDefaults::scalingSupersampling);
    static_assert(ls::GameConfDefaults::scalingSharpness == 0.8F);
    static_assert(ls::GameConfLimits::minimumScalingFactor == 1.0F);
    static_assert(ls::GameConfLimits::maximumScalingFactor == 2.0F);
    static_assert(ls::GameConfLimits::minimumScalingSharpness == 0.0F);
    static_assert(ls::GameConfLimits::maximumScalingSharpness == 1.0F);
    ls::GameConf ultra_scaling;
    ultra_scaling.frame_generation_model = ls::FrameGenerationModel::LosslessScaling;
    ultra_scaling.scaling_enabled = true;
    ultra_scaling.scaling_method = ls::ScalingMethod::Ls1;
    ultra_scaling.ultra_performance = true;
    require(ls::effectiveScalingMethod(ultra_scaling) ==
            ls::ScalingMethod::Mako,
        "Ultra Performance does not select MAKO Scaler for Scaling");
    ultra_scaling.frame_generation_model = ls::FrameGenerationModel::MakoFg;
    require(ls::effectiveScalingMethod(ultra_scaling) ==
            ls::ScalingMethod::Mako,
        "Ultra Performance does not select MAKO Scaler with MAKO FG");

    require_property("mako_fg_mode", "QString", true, false);
    require_property("scaling_enabled", "bool", true, false);
    require_property(
        "swapchain_image_count_compatibility", "bool", true, false
    );
    require_property(
        "mailbox_presentation_compatibility", "bool", true, false
    );
    require_property("scaling_method", "QString", true, false);
    require_property("scaling_factor", "float", true, false);
    require_property("scaling_supersampling", "bool", true, false);
    require_property("scaling_sharpness", "float", true, false);
    require_property("minimum_scaling_factor", "float", false, true);
    require_property("maximum_scaling_factor", "float", false, true);
    require_property("minimum_scaling_sharpness", "float", false, true);
    require_property("maximum_scaling_sharpness", "float", false, true);
}

void test_lossless_model_inspection() {
    require_property("lsfg_unavailable", "bool", false, false);
    require_property("ls1_unavailable", "bool", false, false);
    require_property("model_inspection_unavailable", "bool", false, false);
    QTemporaryDir directory;
    require(directory.isValid(), "model inspection fixture failed");
    const auto previousConfig = qgetenv("MAKO_CONFIG"), previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME"), previousPath = qgetenv("PATH");
    qputenv("MAKO_CONFIG", directory.filePath("conf.toml").toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launch.toml").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("PATH", directory.path().toUtf8() + ':' + previousPath);
    const auto dll = directory.filePath("Lossless.dll"), command = directory.filePath("mako-cli");
    QFile input(dll);
    require(input.open(QIODevice::WriteOnly) && input.write("synthetic file") > 0, "cannot create inspection DLL fixture");
    input.close();
    const auto writeCommand = [&](const QByteArray& body) {
        QFile file(command);
        require(file.open(QIODevice::WriteOnly), "cannot write inspector fixture");
        file.write("#!/bin/sh\n");
        file.write(body);
        file.close();
        require(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner),
            "cannot make inspector fixture executable");
    };
    const auto fixture = [&] {
        ls::ConfigFile config;
        config.global().dll = dll.toStdString();
        ls::GameConf game;
        game.name = "Inspected";
        game.frame_generation_model = ls::FrameGenerationModel::LosslessScaling;
        game.scaling_enabled = true;
        game.scaling_method = ls::ScalingMethod::Ls1;
        game.power_profiles = {game, game};
        config.profiles() = {game};
        config.write(directory.filePath("conf.toml").toStdString());
    };
    const auto wait = [](const std::function<bool()>& complete, qint64 limit = 5000) {
        QElapsedTimer timer;
        timer.start();
        while (!complete() && timer.elapsed() < limit) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        require(complete(), "model inspection did not complete");
    };
    fixture();
    writeCommand("case \"$*\" in *--lsfg*) printf '%s\\n' '{\"schema_version\":1,\"compatible\":false}'; exit 1;; esac\n"
        "printf '%s\\n' '{\"schema_version\":1,\"compatible\":true}'\n");
    {
        mako::ui::Backend backend;
        require(!backend.isLosslessScalingMissing() && !backend.isModelInspectionUnavailable(),
            "a pending inspection was presented as a failure");
        wait([&] { return backend.getFrameGenerationModel() == QStringLiteral("mako-fg"); });
        require(backend.losslessScalingFallback() && backend.isLsfgUnavailable() && !backend.isLs1Unavailable() &&
                backend.getScalingMethod() == QStringLiteral("ls1"),
            "an LSFG failure must warn and repair only Frame Generation");
        backend.powerModeSelected(1);
        require(backend.getFrameGenerationModel() == QStringLiteral("mako-fg"),
            "a confirmed unavailable model remained selected in the edited power mode");
    }
    const auto saved = ls::ConfigFile(directory.filePath("conf.toml").toStdString()).profiles().front();
    require(saved.frame_generation_model == ls::FrameGenerationModel::MakoFg &&
            saved.scaling_method == ls::ScalingMethod::Ls1 &&
            saved.power_profiles[0].frame_generation_model == ls::FrameGenerationModel::MakoFg &&
            saved.power_profiles[1].frame_generation_model == ls::FrameGenerationModel::LosslessScaling,
        "inspection fallback did not persist exactly the edited models and power modes");

    fixture();
    writeCommand("case \"$*\" in *--ls1*) printf '%s\\n' '{\"schema_version\":1,\"compatible\":false}'; exit 1;; esac\n"
        "printf '%s\\n' '{\"schema_version\":1,\"compatible\":true}'\n");
    {
        mako::ui::Backend backend;
        wait([&] { return backend.getScalingMethod() == QStringLiteral("mako"); });
        require(backend.losslessScalingFallback() && backend.isLs1Unavailable() && !backend.isLsfgUnavailable() &&
                backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling"),
            "an LS1 failure must preserve working LSFG");
        writeCommand("printf '%s\\n' '{\"schema_version\":1,\"compatible\":true}'\n");
        backend.refreshLosslessScaling();
        require(!backend.isLs1Unavailable() && !backend.losslessScalingFallback(),
            "inspector replacement did not invalidate the failed cached result");
    }

    // Crashes, old schemas, malformed output, and exit/protocol disagreement are
    // unknown. They must explain uncertainty without disabling or rewriting LS.
    for (const auto& response : {
            QByteArray("exit 2\n"),
            QByteArray("printf '%s\\n' 'not JSON'\n"),
            QByteArray("printf '%070000d' 0\n"),
            QByteArray("printf '%s\\n' '{\"schema_version\":2,\"compatible\":false}'; exit 1\n"),
            QByteArray("printf '%s\\n' '{\"schema_version\":1,\"compatible\":false}'; exit 0\n")}) {
        fixture();
        writeCommand(response);
        mako::ui::Backend backend;
        wait([&] { return backend.isModelInspectionUnavailable(); });
        require(!backend.losslessScalingFallback() && !backend.isLsfgUnavailable() && !backend.isLs1Unavailable() &&
                backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling") &&
                backend.getScalingMethod() == QStringLiteral("ls1"),
            "an inconclusive inspection changed or disabled a saved model");
    }

    fixture();
    {
        QFile file(command);
        require(file.open(QIODevice::WriteOnly) && file.write("#!/missing-mako-inspector-interpreter\n") > 0,
            "cannot create an unavailable inspector fixture");
        file.close();
        mako::ui::Backend backend;
        wait([&] { return backend.isModelInspectionUnavailable(); });
        require(!backend.losslessScalingFallback() && !backend.isLsfgUnavailable() &&
                backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling"),
            "an inspector that cannot start changed the selected model");
    }

    fixture();
    writeCommand("exec /usr/bin/sleep 30\n");
    {
        mako::ui::Backend backend;
        wait([&] { return backend.isModelInspectionUnavailable(); }, 18000);
        require(!backend.losslessScalingFallback() && !backend.isLsfgUnavailable() &&
                backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling"),
            "an inspector timeout changed the selected model");
    }

    fixture();
    const auto started = directory.filePath("started");
    writeCommand("case \"$*\" in *--lsfg*) : > '" + started.toUtf8() + "'; /usr/bin/sleep 0.6; "
        "printf '%s\\n' '{\"schema_version\":1,\"compatible\":false}'; exit 1;; esac\n"
        "printf '%s\\n' '{\"schema_version\":1,\"compatible\":true}'\n");
    {
        mako::ui::Backend backend;
        wait([&] { return QFileInfo::exists(started); });
        // The response from the old DLL must not repair the new selection.
        backend.dllUpdated(dll + QStringLiteral(".new"));
        require(backend.isLosslessScalingMissing(), "missing replacement DLL not detected");
        require(QFile::copy(dll, dll + QStringLiteral(".new")), "cannot replace the inspected DLL");
        writeCommand("printf '%s\\n' '{\"schema_version\":1,\"compatible\":true}'\n");
        backend.refreshLosslessScaling();
        backend.frameGenerationModelUpdated(QStringLiteral("lossless-scaling"));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1200) { QCoreApplication::processEvents(); QThread::msleep(5); }
        require(!backend.losslessScalingFallback() && !backend.isLsfgUnavailable() &&
                backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling"),
            "a stale inspection rewrote the replacement DLL's model selection");
    }
    fixture();
    QFile::remove(started);
    writeCommand(": > '" + started.toUtf8() + "'\nexec /usr/bin/sleep 30\n");
    {
        mako::ui::Backend backend;
        backend.scalingMethodUpdated(QStringLiteral("mako"));
        wait([&] { return QFileInfo::exists(started); });
        backend.frameGenerationModelUpdated(QStringLiteral("mako-fg"));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 400) { QCoreApplication::processEvents(); QThread::msleep(5); }
        require(!backend.isModelInspectionUnavailable() && !backend.losslessScalingFallback(),
            "cancelling inspection for a MAKO selection produced a spurious notice");
        // No inspector process may restart while both MAKO models are selected.
        QFile::remove(started);
        backend.refreshLosslessScaling();
        timer.restart();
        while (timer.elapsed() < 400) { QCoreApplication::processEvents(); QThread::msleep(5); }
        require(!QFileInfo::exists(started), "MAKO-only settings started licensed-model inspection");
    }
    for (const auto& entry : {std::pair{"MAKO_CONFIG", previousConfig}, std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch},
            std::pair{"XDG_CONFIG_HOME", previousConfigHome}, std::pair{"PATH", previousPath}}) {
        if (entry.second.isNull()) qunsetenv(entry.first); else qputenv(entry.first, entry.second);
    }
}

void test_lossless_scaling_is_optional() {
    require_property("lossless_scaling_missing", "bool", false, false);
    require_property("lossless_scaling_fallback", "bool", false, false);
    QTemporaryDir directory;
    require(directory.isValid(), "optional Lossless Scaling fixture failed");
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto previousShaders = qgetenv("MAKO_VKBASALT_SHADER_DIR");
    qputenv("MAKO_CONFIG", directory.filePath("conf.toml").toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launch.toml").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("MAKO_VKBASALT_SHADER_DIR", QByteArray(MAKO_UI_VKBASALT_SHADER_SOURCE_DIR));
    const auto dll = directory.filePath("Lossless.dll");
    ls::ConfigFile fixture;
    ls::GlobalConf global;
    global.dll = dll.toStdString();
    ls::GameConf game;
    game.name = "optional";
    game.frame_generation_model = ls::FrameGenerationModel::LosslessScaling;
    game.scaling_method = ls::ScalingMethod::Ls1;
    game.power_profiles = {game, game};
    fixture.global() = global;
    fixture.profiles() = {game};
    fixture.write(directory.filePath("conf.toml").toStdString());
    {
        mako::ui::Backend backend;
        require(backend.isLosslessScalingMissing() && backend.isValidProfileIndex(),
            "a missing DLL must not block the Qt profile editor");
        require(backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling") &&
                backend.getScalingMethod() == QStringLiteral("ls1") && backend.losslessScalingFallback(),
            "a missing DLL must keep the saved models, since it can return, and show a warning");
        backend.powerModeSelected(1);
        require(backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling") &&
                backend.getScalingMethod() == QStringLiteral("ls1") && backend.losslessScalingFallback(),
            "a missing DLL replaced a saved model in an edited power profile");
        backend.frameGenerationModelUpdated(QStringLiteral("mako-fg"));
        backend.powerModeSelected(0);
        backend.frameGenerationModelUpdated(QStringLiteral("mako-fg"));
        backend.frameGenerationProvisionedUpdated(false);
        backend.scalingEnabledUpdated(true);
        backend.scalingMethodUpdated(QStringLiteral("mako"));
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltShaderUpdated(QStringLiteral("vibrance"));
        require(backend.getScalingEnabled() && backend.getScalingMethod() == QStringLiteral("mako") &&
                backend.getEnableVkBasalt() && backend.getVkBasaltShader() == QStringLiteral("vibrance"),
            "model-free scaling and shaders must remain editable without a DLL");
        QFile input(dll);
        require(input.open(QIODevice::WriteOnly) && input.write("synthetic file") > 0,
            "cannot create synthetic DLL availability fixture");
        input.close();
        backend.refreshLosslessScaling();
        require(!backend.isLosslessScalingMissing(), "DLL installation was not detected");
        require(!backend.losslessScalingFallback(), "a restored DLL did not clear the missing-DLL warning");
        backend.frameGenerationModelUpdated(QStringLiteral("lossless-scaling"));
        require(backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling"),
            "an available Lossless Scaling selection must remain valid");
        backend.dllUpdated(dll + QStringLiteral(".missing"));
        require(backend.isLosslessScalingMissing(), "a missing saved DLL path was not authoritative");
        require(backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling") && backend.losslessScalingFallback(),
            "a missing DLL replaced the saved model or hid the warning, and it may only be an unmounted SD card");
        backend.dllUpdated(dll);
        require(!backend.isLosslessScalingMissing(), "DLL path edit did not refresh availability");
        require(backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling") && !backend.losslessScalingFallback(),
            "a returned DLL did not leave the saved model in place and clear the warning");
        require(input.remove() && QDir().mkdir(dll), "cannot create non-file DLL fixture");
        backend.refreshLosslessScaling();
        require(backend.isLosslessScalingMissing(), "a directory was accepted as a DLL");
        backend.frameGenerationModelUpdated(QStringLiteral("mako-fg"));
    }
    const auto saved = ls::ConfigFile(directory.filePath("conf.toml").toStdString()).profiles().front();
    require(saved.scaling_enabled && saved.scaling_method == ls::ScalingMethod::Mako && !saved.frame_generation_provisioned,
        "model-free profile settings did not persist without Lossless Scaling");
    require(saved.frame_generation_model == ls::FrameGenerationModel::MakoFg &&
            saved.power_profiles[0].frame_generation_model == ls::FrameGenerationModel::MakoFg &&
            saved.power_profiles[1].frame_generation_model == ls::FrameGenerationModel::LosslessScaling,
        "a model edit did not persist exactly the edited power profile");
    {
        mako::ui::Backend reopened;
        require(!reopened.losslessScalingFallback(),
            "a profile already using MAKO must not get a new fallback warning");
        require(reopened.getEnableVkBasalt() && reopened.getVkBasaltShader() == QStringLiteral("vibrance"),
            "shader settings did not persist without Lossless Scaling");
    }
    for (const auto& entry : {std::pair{"MAKO_CONFIG", previousConfig},
            std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch},
            std::pair{"XDG_CONFIG_HOME", previousConfigHome},
            std::pair{"MAKO_VKBASALT_SHADER_DIR", previousShaders}}) {
        if (entry.second.isNull()) qunsetenv(entry.first);
        else qputenv(entry.first, entry.second);
    }
}

void test_power_profile_editor() {
    require_property("gpu_headroom_percent", "uint", true, false);
    require_property("adaptive_target_refresh_rate", "bool", true, false);
    require_property("separate_power_modes", "bool", true, false);
    require_property("power_mode", "int", true, false);
    QTemporaryDir directory;
    require(directory.isValid(), "power profile fixture failed");
    const auto path = directory.filePath("conf.toml");
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    qputenv("MAKO_CONFIG", path.toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launch.toml").toUtf8());
    ls::ConfigFile fixture;
    const auto availableDll = directory.filePath("available.dll");
    QFile dllFixture(availableDll);
    require(dllFixture.open(QIODevice::WriteOnly) && dllFixture.write("synthetic DLL availability fixture") > 0,
        "cannot create the available-model fixture");
    dllFixture.close();
    fixture.global().dll = availableDll.toStdString();
    ls::GameConf game;
    game.name = "game";
    game.target_fps = 90;
    game.active_in = {"Game.exe"};
    fixture.global().allow_fp16 = false;
    fixture.profiles() = {game};
    fixture.write(path.toStdString());
    {
        mako::ui::Backend backend;
        backend.separatePowerModesUpdated(true);
        backend.powerModeSelected(1);
        require(!backend.getAdaptiveTargetRefreshRate(),
            "Refresh targeting must be off for an existing profile");
        backend.adaptiveTargetRefreshRateUpdated(true);
        require(backend.getTargetFPS() == 90, "Handheld did not clone shared settings");
        require(backend.getFrameGenerationModel() == QStringLiteral("mako-fg"), "Qt profiles default to MAKO FG");
        backend.frameGenerationModelUpdated(QStringLiteral("lossless-scaling"));
        backend.frameGenerationModelUpdated(QStringLiteral("unknown"));
        require(backend.getFrameGenerationModel() == QStringLiteral("lossless-scaling"), "Qt accepts only supported model choices");
        backend.targetFPSUpdated(60);
        backend.baseFPSCapUpdated(30);
        backend.gpuHeadroomPercentUpdated(15);
        require(backend.getGpuHeadroomPercent() == 15, "Headroom setter failed");
        backend.gpuHeadroomPercentUpdated(7);
        require(backend.getGpuHeadroomPercent() == 15, "Invalid headroom must be rejected");
        backend.scalingEnabledUpdated(true);
        backend.scalingMethodUpdated(QStringLiteral("mako"));
        backend.scalingSharpnessUpdated(0.2F);
        backend.flowScaleUpdated(0.75F);
        backend.powerModeSelected(2);
        require(!backend.getAdaptiveTargetRefreshRate(),
            "Handheld refresh targeting changed Docked settings");
        require(backend.getFrameGenerationModel() == QStringLiteral("mako-fg"), "Model choice leaked to another power profile");
        require(backend.getTargetFPS() == 90, "Handheld edits changed Docked settings");
        require(backend.getGpuHeadroomPercent() == 0 &&
                backend.getBaseFPSCap() == ls::GameConfDefaults::baseFpsCap &&
                !backend.getScalingEnabled() &&
                backend.getFlowScale() == ls::GameConfDefaults::flowScale,
            "Handheld live tuning leaked into Docked settings");
        backend.targetFPSUpdated(144);
        backend.baseFPSCapUpdated(90);
        backend.scalingEnabledUpdated(true);
        backend.scalingMethodUpdated(QStringLiteral("ls1"));
        backend.scalingSharpnessUpdated(0.7F);
        backend.flowScaleUpdated(0.8F);
        backend.addActiveIn(QStringLiteral("New.exe"));
        backend.renameProfile(QStringLiteral("renamed"));
        backend.powerModeSelected(0);
        require(backend.getTargetFPS() == 90 && !backend.getAllowFP16(),
            "Power settings changed fallback or global precision");
    }
    const ls::ConfigFile saved(path.toStdString());
    const auto& base = saved.profiles().front();
    require(base.power_profiles[0].frame_generation_model == ls::FrameGenerationModel::LosslessScaling &&
            base.power_profiles[1].frame_generation_model == ls::FrameGenerationModel::MakoFg &&
            base.frame_generation_model == ls::FrameGenerationModel::MakoFg,
        "Qt did not persist independent model choices");
    require(base.name == "renamed" && base.active_in.size() == 2 &&
            base.power_profiles[0].target_fps == 60 &&
            base.power_profiles[0].gpu_headroom_percent == 15 &&
            base.power_profiles[0].adaptive_target_refresh_rate &&
            !base.power_profiles[1].adaptive_target_refresh_rate &&
            !base.adaptive_target_refresh_rate &&
            base.power_profiles[1].target_fps == 144 &&
            base.power_profiles[0].base_fps_cap == 30 &&
            base.power_profiles[1].base_fps_cap == 90 &&
            base.power_profiles[0].scaling_method == ls::ScalingMethod::Mako &&
            base.power_profiles[1].scaling_method == ls::ScalingMethod::Ls1 &&
            base.power_profiles[0].scaling_sharpness == 0.2F &&
            base.power_profiles[1].scaling_sharpness == 0.7F &&
            base.power_profiles[0].flow_scale == 0.75F &&
            base.power_profiles[1].flow_scale == 0.8F &&
            base.base_fps_cap == ls::GameConfDefaults::baseFpsCap &&
            base.flow_scale == ls::GameConfDefaults::flowScale,
        "Qt did not save separate native settings with shared identity");
    {
        mako::ui::Backend backend;
        backend.powerModeSelected(2);
        require(backend.getTargetFPS() == 144, "Qt did not reload Decky-compatible power tables");
        backend.separatePowerModesUpdated(false);
        require(backend.getTargetFPS() == 90 && backend.getPowerMode() == 0,
            "Disabling separate settings did not restore the fallback");
    }
    require(ls::ConfigFile(path.toStdString()).profiles().front().power_profiles.empty(),
        "Qt left disabled power tables behind");
    if (previousConfig.isNull()) qunsetenv("MAKO_CONFIG");
    else qputenv("MAKO_CONFIG", previousConfig);
    if (previousLaunch.isNull()) qunsetenv("MAKO_LAUNCH_CONFIG");
    else qputenv("MAKO_LAUNCH_CONFIG", previousLaunch);
}

void test_multiplier_limits() {
    static_assert(ls::GameConfLimits::minimumMultiplier == 2);
    static_assert(ls::GameConfLimits::maximumMultiplier == 5);
    static_assert(ls::GameConfLimits::minimumAdaptiveMaxMultiplier == 2);
    static_assert(ls::GameConfLimits::maximumAdaptiveMaxMultiplier == 5);

    require_property("minimum_multiplier", "uint", false, true);
    require_property("maximum_multiplier", "uint", false, true);
    require_property("minimum_adaptive_max_multiplier", "uint", false, true);
    require_property("maximum_adaptive_max_multiplier", "uint", false, true);
    require_property("frame_generation_provisioned", "bool", true, false);
    require_property("frame_generation_model", "QString", true, false);
    require_property("frame_generation_enabled", "bool", true, false);
    require_property("gamescope_vrr_mode", "QString", true, false);
    require_property("frame_generation_factor_index", "uint", true, false);

    ls::GameConf configuration;
    require(mako::ui::Backend::frameGenerationFactorIndex(configuration) == 1,
        "Default Fixed 2x did not map to the first active factor choice");
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 0);
    require(!configuration.frame_generation_enabled &&
            configuration.multiplier == 2,
        "0x did not pause Fixed generation while preserving its multiplier");
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 4);
    require(configuration.frame_generation_enabled &&
            configuration.multiplier == 5,
        "The highest Fixed factor choice did not select 5x");
    configuration.adaptive = true;
    configuration.adaptive_max_multiplier = 4;
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 0);
    require(!configuration.frame_generation_enabled &&
            configuration.adaptive && configuration.multiplier == 5 &&
            configuration.adaptive_max_multiplier == 4,
        "Adaptive 0x did not preserve its mode and saved multipliers");
    mako::ui::Backend::applyFrameGenerationFactorIndex(configuration, 3);
    require(configuration.frame_generation_enabled &&
            configuration.adaptive && configuration.multiplier == 5 &&
            configuration.adaptive_max_multiplier == 4 &&
            mako::ui::Backend::frameGenerationFactorIndex(configuration) == 3,
        "Adaptive could not resume at its selected maximum multiplier");

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral(
            "currentIndex: backend.frame_generation_factor_index")) &&
            qml.contains(QStringLiteral(
                "onActivated: index => backend.frame_generation_factor_index = index")) &&
            qml.contains(QStringLiteral(
                "[\"0\" + t.multiplierX, \"2\" + t.multiplierX, \"3\" + t.multiplierX, \"4\" + t.multiplierX, \"5\" + t.multiplierX]")),
        "Frame Generation multipliers do not expose the shared 0x and 2x-5x choices");
    require(qml.contains(QStringLiteral(
            "visible: backend.frame_generation_provisioned && backend.adaptive")) &&
            qml.contains(QStringLiteral(
                "visible: backend.frame_generation_provisioned && !backend.adaptive")),
        "Qt does not switch between the Adaptive and Fixed multiplier controls");
}

void test_fractional_adaptive_preset() {
    require_property("fractional_adaptive", "bool", true, false);
    require_property(
        "adaptive_fractional_real_frame_priority", "QString", true, false
    );
    require_property(
        "adaptive_fractional_real_frame_priority_cap", "double", false, false
    );
    require(mako::ui::Backend::staticMetaObject.indexOfMethod(
                "fractionalRealFramePriorityCapFor(QString)") >= 0,
        "Qt does not expose priority-cap previews to QML");
    require(ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::Auto, 120) == 0.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::Low, 120) == 72.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::Medium, 120) == 80.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::High, 120) == 90.0 &&
            ls::adaptiveFractionalRealFramePriorityCap(
                ls::AdaptiveFractionalRealFramePriority::VeryHigh, 120) == 96.0,
        "Fractional real-frame priorities lost their cadence-friendly ratios");

    ls::GameConf configuration;
    configuration.frame_generation_enabled = false;
    configuration.adaptive = false;
    configuration.adaptive_auto_base_fps_cap = true;
    configuration.dynamic_cadence_recovery = true;
    mako::ui::Backend::applyFractionalAdaptivePreset(configuration, true);
    require(!configuration.frame_generation_enabled,
        "Editing Fractional Adaptive unexpectedly resumed Frame Generation from 0x");
    require(configuration.adaptive,
        "Fractional Adaptive did not enable Adaptive Frame Generation");
    require(!configuration.adaptive_auto_base_fps_cap,
        "Fractional Adaptive did not disable Steady Base Cap");
    require(!configuration.dynamic_cadence_recovery,
        "Fractional Adaptive did not disable Dynamic Cadence Recovery");
    require(mako::ui::Backend::isFractionalAdaptivePresetEnabled(configuration),
        "Fractional Adaptive state was not recognized after enabling it");

    configuration.dynamic_cadence_recovery = true;
    mako::ui::Backend::applyFractionalAdaptivePreset(configuration, false);
    require(configuration.adaptive_auto_base_fps_cap,
        "Disabling Fractional Adaptive did not restore Steady Base Cap");
    require(!configuration.dynamic_cadence_recovery,
        "Disabling Fractional Adaptive did not disable Dynamic Cadence Recovery");
    require(!mako::ui::Backend::isFractionalAdaptivePresetEnabled(configuration),
        "Fractional Adaptive state remained enabled after disabling it");

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral("title: t.fractionalAdaptive")),
        "Fractional Adaptive control is missing from the Renderer UI");
    require(qml.contains(QStringLiteral("checked: backend.fractional_adaptive")),
        "Fractional Adaptive control does not read the atomic preset property");
    require(qml.contains(QStringLiteral(
            "onToggled: backend.fractional_adaptive = checked")),
        "Fractional Adaptive control does not update the atomic preset property");
    require(qml.contains(QStringLiteral(
            "visible: backend.frame_generation_provisioned\n"
            "                            && backend.fractional_adaptive")) &&
            qml.contains(QStringLiteral(
                "backend.fractionalRealFramePriorityCapFor(id)")) &&
            qml.contains(QStringLiteral(
                "priorityOption(t.high, \"high\", backend.target_fps)")) &&
            qml.contains(QStringLiteral(
                "backend.adaptive_fractional_real_frame_priority !== \"auto\"")),
        "Fractional priority is not scoped to Fractional Adaptive or does not own its explicit cap");
}

void test_feature_group_order_and_ownership() {
    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());

    const qsizetype group_start = qml.indexOf(
        QStringLiteral("name: t.scalingSettings")
    );
    require(group_start >= 0, "independent Scaling group is missing");
    const qsizetype frame_generation_group_start = qml.indexOf(
        QStringLiteral("name: t.frameGeneration")
    );
    require(frame_generation_group_start >= 0,
        "Frame Generation group is missing");
    const qsizetype performance_group_start = qml.indexOf(
        QStringLiteral("name: t.performanceSettings")
    );
    require(performance_group_start >= 0,
        "Performance Settings group is missing");
    require(frame_generation_group_start < group_start &&
            group_start < performance_group_start,
        "Frame Generation, Scaling, and Performance Settings are out of order");

    const QString frame_generation_group = qml.mid(
        frame_generation_group_start,
        group_start - frame_generation_group_start
    );
    require(frame_generation_group.count(QStringLiteral("GroupEntry {")) == 15 &&
            frame_generation_group.count(QStringLiteral(
                "visible: backend.frame_generation_provisioned")) == 14,
        "Frame Generation must retain its provisioning switch while keeping live controls visible at 0x");
    require(frame_generation_group.contains(QStringLiteral(
                "title: t.gamescopeVrrMode")),
        "Frame Generation must expose its Gamescope VRR choice");
    require(frame_generation_group.contains(QStringLiteral(
                "checked: backend.adaptive_target_refresh_rate")) &&
            frame_generation_group.contains(QStringLiteral(
                "onToggled: backend.adaptive_target_refresh_rate = checked")) &&
            frame_generation_group.contains(QStringLiteral("? t.fallbackTargetFps : t.targetFps")),
        "Adaptive must expose refresh matching and its saved fallback");
    const qsizetype target_entry = frame_generation_group.indexOf(
        QStringLiteral("title: backend.adaptive_target_refresh_rate")
    );
    const qsizetype matching_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.adaptiveTargetRefreshRate")
    );
    require(target_entry >= 0 && matching_entry > target_entry &&
            frame_generation_group.mid(target_entry, matching_entry - target_entry)
                .count(QStringLiteral("GroupEntry {")) == 1 &&
            frame_generation_group.mid(target_entry, matching_entry - target_entry)
                .contains(QStringLiteral("enabled: !backend.adaptive_target_refresh_rate")),
        "Refresh matching must directly follow the target and lock manual editing while enabled");
    require(frame_generation_group.contains(QStringLiteral(
                "checked: backend.frame_generation_provisioned")) &&
            frame_generation_group.contains(QStringLiteral(
                "onToggled: backend.frame_generation_provisioned = checked")),
        "Frame Generation provisioning is not exposed as the restart-bound switch");
    require(!frame_generation_group.contains(QStringLiteral(
                "enabled: backend.frame_generation_enabled")),
        "A dormant live Frame Generation setting cannot be prepared while the factor is 0x");
    require(!qml.contains(QStringLiteral("title: t.performanceMode")),
        "Lighter FG Model must be an LSFG entry in the model picker, not a separate toggle");
    const qsizetype model_picker_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.fgModel")
    );
    const qsizetype adaptive_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.adaptiveFrameGen")
    );
    const QString model_picker = frame_generation_group.mid(
        model_picker_entry, adaptive_entry - model_picker_entry
    );
    require(model_picker_entry >= 0 && adaptive_entry > model_picker_entry &&
            model_picker.contains(QStringLiteral(
                "[\"mako-fg\", \"lsfg-quality\", \"lsfg-performance\"]")) &&
            model_picker.contains(QStringLiteral(
                "backend.ultra_performance\n                                ? [\"mako-fg\", \"lsfg-performance\"]")) &&
            model_picker.contains(QStringLiteral("t.fgModelLsfgQuality")) &&
            model_picker.contains(QStringLiteral("t.fgModelLsfgPerformance")) &&
            model_picker.contains(QStringLiteral(
                "backend.performance_mode = choice === \"lsfg-performance\"")),
        "The model picker must offer MAKO FG, LSFG Quality and LSFG Performance, hiding Quality under Ultra Performance");
    const qsizetype fixed_multiplier_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.multiplier")
    );
    const qsizetype adaptive_multiplier_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.maxAdaptiveMultiplier")
    );
    const qsizetype smooth_cadence_entry = frame_generation_group.indexOf(
        QStringLiteral("title: t.smoothCadence")
    );
    require(adaptive_multiplier_entry >= 0 &&
            adaptive_multiplier_entry < fixed_multiplier_entry &&
            fixed_multiplier_entry < smooth_cadence_entry,
        "Frame Generation must order its conditional multipliers before Smooth Cadence");

    qsizetype group_end = qml.indexOf(
        QStringLiteral("\n                Group {"), group_start + 1
    );
    if (group_end < 0)
        group_end = qml.size();
    const QString scaling_group = qml.mid(group_start, group_end - group_start);

    require(scaling_group.contains(QStringLiteral("backend.scaling_enabled")),
        "Scaling group does not bind the enable property");
    require(scaling_group.contains(QStringLiteral("backend.scaling_method")),
        "Scaling group does not bind the method property");
    require(scaling_group.contains(QStringLiteral("backend.scaling_factor")),
        "Scaling group does not bind the factor property");
    require(scaling_group.contains(
                QStringLiteral("backend.scaling_supersampling")),
        "Scaling group does not bind the supersampling property");
    require(scaling_group.contains(QStringLiteral("backend.scaling_sharpness")),
        "Scaling group does not bind the sharpness property");
    require(scaling_group.count(
            QStringLiteral("visible: backend.scaling_enabled")) == 4 &&
            scaling_group.count(QStringLiteral(
                "visible: backend.scaling_enabled && backend.scaling_method !== \"native\"")) == 1,
        "Native must retain Scale Factor while hiding model-only sharpness");
    require(scaling_group.contains(QStringLiteral(
            "model: [t.scalingMethodNative, t.scalingMethodMako, t.scalingMethodLs1, t.scalingMethodLs1Performance]")),
        "Scaling method order must expose Native before every scaler");
    require(scaling_group.contains(QStringLiteral(
            "enabled: backend.scaling_enabled && !backend.ultra_performance")),
        "Ultra Performance must visibly lock the Renderer scaling method");
    require(scaling_group.contains(QStringLiteral(
            "return backend.ultra_performance ? 1")),
        "Ultra Performance must visibly select MAKO Scaler");
    require(!scaling_group.contains(QStringLiteral("backend.adaptive")),
        "Scaling group is coupled to Adaptive");
    require(!scaling_group.contains(
            QStringLiteral("backend.frame_generation_enabled")),
        "Scaling group is coupled to Frame Generation");
    require(!scaling_group.contains(
            QStringLiteral("backend.frame_generation_provisioned")),
        "Scaling group is coupled to Frame Generation provisioning");

    const qsizetype compatibility_group_start = qml.indexOf(
        QStringLiteral("name: t.compatibilitySettings")
    );
    require(compatibility_group_start >= 0,
        "Compatibility Settings group is missing");
    qsizetype compatibility_group_end = qml.indexOf(
        QStringLiteral("\n                Group {"),
        compatibility_group_start + 1
    );
    if (compatibility_group_end < 0)
        compatibility_group_end = qml.size();
    const QString compatibility_group = qml.mid(
        compatibility_group_start,
        compatibility_group_end - compatibility_group_start
    );
    require(compatibility_group.contains(QStringLiteral(
                "checked: backend.swapchain_image_count_compatibility")) &&
            compatibility_group.contains(QStringLiteral(
                "onToggled: backend.swapchain_image_count_compatibility = checked")),
        "Compatibility Settings does not expose the restart-bound swapchain image policy");
    require(compatibility_group.contains(QStringLiteral(
                "checked: backend.mailbox_presentation_compatibility")) &&
            compatibility_group.contains(QStringLiteral(
                "onToggled: backend.mailbox_presentation_compatibility = checked")),
        "Compatibility Settings does not expose the restart-bound Mailbox presentation policy");
}

void test_shader_controls() {
    require_property("enable_vkbasalt", "bool", true, false);
    require_property("vkbasalt_sharpening", "QString", true, false);
    require_property("vkbasalt_sharpness", "float", true, false);
    require_property("vkbasalt_dls_denoise", "float", true, false);
    require_property("vkbasalt_antialiasing", "QString", true, false);
    require_property("vkbasalt_shader", "QString", true, false);
    require_property("vkbasalt_config_path", "QString", false, false);
    require_property("custom_shader_effects", "QVariantList", false, false);
    require_property("shader_load_error", "QString", false, false);
    require(mako::ui::Backend::staticMetaObject.indexOfMethod("addCustomShader(QString)") >= 0 &&
            mako::ui::Backend::staticMetaObject.indexOfMethod("refreshCustomShaders()") >= 0 &&
            mako::ui::Backend::staticMetaObject.indexOfMethod("deleteSelectedCustomShaders()") >= 0,
        "Qt custom shader controls are not exposed");
    require(mako::ui::Backend::staticMetaObject.indexOfMethod(
                "openVkBasaltConfig()") >= 0,
        "Qt backend does not expose the advanced shader configuration opener");
    require_property("launch_option", "QString", false, false);
    require_property("enable_hdr", "bool", true, false);
    require_property("minimum_vkbasalt_strength", "float", false, true);
    require_property("maximum_vkbasalt_strength", "float", false, true);

    QFile file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString qml = QString::fromUtf8(file.readAll());
    require(qml.contains(QStringLiteral("text: t.deleteSelectedCustomShaders")) &&
            qml.contains(QStringLiteral("onClicked: backend.deleteSelectedCustomShaders()")) &&
            qml.contains(QStringLiteral("palette.button: \"#64253a\"")),
        "Qt custom shader deletion action or danger styling is missing");
    require(qml.contains(QStringLiteral("name: t.shaderSettings")) &&
            qml.contains(QStringLiteral("checked: backend.enable_vkbasalt")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_shader")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_sharpening")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_sharpness")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_dls_denoise")) &&
            qml.contains(QStringLiteral("backend.vkbasalt_antialiasing")) &&
            qml.contains(QStringLiteral(
                "onClicked: backend.openVkBasaltConfig()")) &&
            qml.contains(QStringLiteral("text: backend.vkbasalt_config_path")),
        "Qt shader group does not expose the Decky-compatible compact controls");
}

void test_compact_restart_markers() {
    QFile ui_file(QString::fromUtf8(MAKO_UI_QML_FILE));
    require(ui_file.open(QIODevice::ReadOnly), "MAKO UI QML could not be opened");
    const QString ui_qml = QString::fromUtf8(ui_file.readAll());
    require(ui_qml.count(QStringLiteral("compactRestartMarker: true")) == 16,
        "Every restart-bound Renderer control must opt into the compact marker");

    QFile entry_file(QString::fromUtf8(MAKO_UI_GROUP_ENTRY_QML_FILE));
    require(entry_file.open(QIODevice::ReadOnly),
        "MAKO GroupEntry QML could not be opened");
    const QString entry_qml = QString::fromUtf8(entry_file.readAll());
    require(entry_qml.contains(QStringLiteral(
            "property bool compactRestartMarker: false")),
        "GroupEntry does not expose the compact restart-marker contract");
    require(entry_qml.contains(QStringLiteral("font-size: 72%")),
        "Restart marker is not rendered at the compact size");
}

void test_save_lifetime() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary configuration directory failed");
    const auto configPath = directory.filePath("conf.toml").toStdString();
    const auto launchPath = directory.filePath("launcher.conf").toStdString();
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto previousShaderDirectory = qgetenv("MAKO_VKBASALT_SHADER_DIR");
    qputenv("MAKO_CONFIG", QByteArray::fromStdString(configPath));
    qputenv("MAKO_LAUNCH_CONFIG", QByteArray::fromStdString(launchPath));
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("MAKO_VKBASALT_SHADER_DIR",
        QByteArray(MAKO_UI_VKBASALT_SHADER_SOURCE_DIR));
    {
        mako::ui::Backend backend;
        require(backend.getHdrQuality() == QStringLiteral("performance") && !backend.getEnableHdr(),
            "Qt must default reduced precision on while HDR remains disabled");
        backend.allowFP16Updated(false);
        for (const bool enabled : {true, false}) {
            backend.ultraPerformanceUpdated(enabled);
            require(!backend.getAllowFP16(),
                "Ultra Performance overwrote an explicit global FP32 choice");
        }
        backend.ultraPerformanceUpdated(true);
        backend.allowFP16Updated(true);
        require(backend.getAllowFP16(),
            "FP16 precision must remain editable under Ultra Performance");
        backend.allowFP16Updated(false);
        require(!backend.getAllowFP16(),
            "FP32 precision must remain editable under Ultra Performance");
        backend.ultraPerformanceUpdated(false);
        backend.targetFPSUpdated(90);
        require(backend.fractionalRealFramePriorityCapFor(QStringLiteral("low")) == 54.0,
            "Priority preview did not follow the initial Target FPS");
        backend.targetFPSUpdated(144);
        require(backend.fractionalRealFramePriorityCapFor(QStringLiteral("high")) == 108.0,
            "Priority preview did not update with Target FPS");
        require(backend.getDisableSteamOverlay(),
            "Steam overlay removal must default to enabled in the Qt UI");
        require(!backend.getEnableHdr(), "HDR must remain disabled by default");
        require(backend.setProperty("enable_hdr", true), "Enable HDR property must be writable");
        require(backend.getEnableHdr(), "Enable HDR On must opt in to HDR");
        backend.disableSteamOverlayUpdated(false);
        backend.enableZinkUpdated(true);
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltSharpeningUpdated(QStringLiteral("dls"));
        backend.vkBasaltSharpnessUpdated(0.75F);
        backend.vkBasaltDlsDenoiseUpdated(0.4F);
        backend.vkBasaltAntialiasingUpdated(QStringLiteral("fxaa"));
        backend.vkBasaltShaderUpdated(QStringLiteral("hdr_look:clarity:vibrance:levels_plus"));
        require(!std::filesystem::exists(configPath), "UI edit was not debounced");
        QEventLoop events;
        QTimer::singleShot(700, &events, &QEventLoop::quit);
        events.exec();
        require(ls::ConfigFile(configPath).profiles().front().target_fps == 144,
            "UI timer did not save the latest edit");
        require(!ls::LaunchConfigFile(launchPath).settings().disable_hdr_exposure,
            "Qt must persist the explicit HDR opt-in");
        {
            mako::ui::Backend persisted;
            require(persisted.getEnableHdr(),
                "A saved disable_hdr_exposure=false must appear as Enable HDR On");
        }
        require(!ls::LaunchConfigFile(launchPath).settings().disable_steam_overlay,
            "UI timer did not persist an explicit overlay opt-out");
        require(ls::LaunchConfigFile(launchPath).settings().enable_zink,
            "UI timer did not save launcher settings");
        const auto vkBasaltPath = directory.filePath(
            "vkbasalt/profile-"
        ).toStdString();
        require(backend.getVkBasaltConfigPath().startsWith(
                    QString::fromStdString(vkBasaltPath)) &&
                std::filesystem::exists(
                    backend.getVkBasaltConfigPath().toStdString()),
            "UI did not create the selected profile's vkBasalt configuration");
        const auto managedShaderDirectory = directory.filePath(
            "vkbasalt/shaders"
        );
        require(QFileInfo::exists(
                    managedShaderDirectory + QStringLiteral("/Vibrance.fx")),
            "UI did not install the shared Decky-compatible shader assets");
        require(QFileInfo::exists(
                    managedShaderDirectory + QStringLiteral("/Clarity.fx")) &&
                QFileInfo::exists(
                    managedShaderDirectory + QStringLiteral("/LevelsPlus.fx")),
            "UI did not install the selectable shader stack assets");
        QFile generatedShaderConfig(backend.getVkBasaltConfigPath());
        require(generatedShaderConfig.open(QIODevice::ReadOnly),
            "UI did not create a readable shader configuration");
        const auto generatedShaderContent = generatedShaderConfig.readAll();
        require(generatedShaderContent.contains(
                    (managedShaderDirectory + QStringLiteral("/Vibrance.fx"))
                        .toUtf8()),
            "UI did not reference the shared Decky-compatible shader directory");
        require(generatedShaderContent.contains(
                    "effects = fxaa:makoHDRLook:makoClarity:makoVibrance:makoLevelsPlus:dls"),
            "UI did not preserve the ordered multi-effect chain in vkBasalt");
        const auto selectedProfile = backend.calculateProfileListModel()
            ->data(backend.calculateProfileListModel()->index(0, 0)).toString();
        require(backend.getLaunchOption().contains(
                    QStringLiteral("ENABLE_VKBASALT=1")) &&
                backend.getLaunchOption().contains(
                    QStringLiteral("MAKO_PROFILE='") + selectedProfile +
                    QStringLiteral("'")) &&
                backend.getLaunchOption().contains(
                    mako::ui::launcherCommandForUiDirectory(
                        QCoreApplication::applicationDirPath()) +
                    QStringLiteral(" %command%")),
            "UI did not produce the profile-specific shader launch option");
        QFile sidecar(directory.filePath(
            "profile-wrapper-settings.json"
        ));
        require(sidecar.open(QIODevice::ReadOnly),
            "UI did not create Decky's profile wrapper settings sidecar");
        const auto storedProfiles = QJsonDocument::fromJson(sidecar.readAll())
            .object().value(QStringLiteral("profiles")).toObject();
        const auto storedProfile = storedProfiles
            .value(selectedProfile).toObject();
        require(storedProfile.value(QStringLiteral("external_vulkan_layer"))
                    .toString() == QStringLiteral("vkbasalt") &&
                storedProfile.value(QStringLiteral("vkbasalt_shader"))
                    .toString() ==
                    QStringLiteral("hdr_look:clarity:vibrance:levels_plus"),
            "UI did not save the ordered effects in Decky's profile sidecar");
        const auto shaderPath = directory.filePath("custom # tone.fx");
        QFile localShader(shaderPath);
        require(localShader.open(QIODevice::WriteOnly), "unable to create custom shader fixture");
        localShader.write("// synthetic shader fixture\n");
        localShader.close();
        const auto priorSelection = backend.getVkBasaltShader();
        require(backend.addCustomShader(QUrl::fromLocalFile(shaderPath).toString()),
            "Qt file URL import failed for a path with spaces and a hash");
        require(backend.getVkBasaltShader() == priorSelection && backend.getCustomShaderEffects().size() == 1,
            "adding a shader must register it without enabling it");
        const auto customId = backend.getCustomShaderEffects().at(0).toMap().value(QStringLiteral("id")).toString();
        require(backend.addCustomShader(shaderPath) && backend.getCustomShaderEffects().size() == 1,
            "Qt custom shader registration must be idempotent");
        backend.vkBasaltShaderUpdated(customId + QStringLiteral(":vibrance"));
        backend.refreshCustomShaders();
        const auto refreshedContent = ls::readVkBasaltConfiguration(backend.getVkBasaltConfigPath().toStdString());
        require(refreshedContent.find("makoReloadGeneration = 1") != std::string::npos,
            "Qt refresh did not request a live source rebuild");
        backend.refreshCustomShaders();
        require(ls::readVkBasaltConfiguration(backend.getVkBasaltConfigPath().toStdString()).find("makoReloadGeneration = 2") != std::string::npos,
            "Qt repeated refresh did not request a new rebuild");
        require(backend.getVkBasaltShader() == customId + QStringLiteral(":vibrance"),
            "refresh discarded a pending custom effect selection");
        backend.vkBasaltShaderUpdated(QStringLiteral("none"));
        backend.refreshCustomShaders();
        require(backend.getCustomShaderEffects().size() == 1 && backend.getVkBasaltShader() == QStringLiteral("none"),
            "clearing a custom shader must retain its selectable definition");
        require(!backend.addCustomShader(shaderPath + QStringLiteral(".missing")) && !backend.getShaderLoadError().isEmpty(),
            "Qt must surface an invalid shader import");
        backend.vkBasaltShaderUpdated(customId);
        backend.refreshCustomShaders();
        QFile removedDefinition(backend.getVkBasaltConfigPath());
        require(removedDefinition.open(QIODevice::ReadOnly), "custom config cannot be read");
        auto withoutDefinition = removedDefinition.readAll();
        removedDefinition.close();
        withoutDefinition.replace((customId.mid(7) + QStringLiteral(" = \"") + shaderPath + QStringLiteral("\"\n")).toUtf8(), QByteArray{});
        require(removedDefinition.open(QIODevice::WriteOnly | QIODevice::Truncate), "custom config cannot be edited");
        removedDefinition.write(withoutDefinition);
        removedDefinition.close();
        backend.vkBasaltShaderUpdated(QStringLiteral("none"));
        backend.refreshCustomShaders();
        require(removedDefinition.open(QIODevice::ReadOnly) &&
                !removedDefinition.readAll().contains(customId.mid(7).toUtf8()),
            "unchecking a shader with a deleted definition left its alias active");
        removedDefinition.close();
        require(backend.addCustomShader(shaderPath), "custom shader could not be registered again");
        backend.vkBasaltShaderUpdated(customId + QStringLiteral(":vibrance"));
        backend.refreshCustomShaders();
        const auto otherShaderPath = directory.filePath("other.fx");
        QFile otherShader(otherShaderPath);
        require(otherShader.open(QIODevice::WriteOnly), "unable to create unselected shader fixture");
        otherShader.write("// unselected shader\n");
        otherShader.close();
        require(backend.addCustomShader(otherShaderPath), "unable to register unselected shader");
        const auto originalProfile = backend.getProfileIndex();
        backend.createProfile(QStringLiteral("Shader Isolation"));
        require(backend.addCustomShader(shaderPath), "unable to register shader in another profile");
        const auto isolatedPath = backend.getVkBasaltConfigPath().toStdString();
        const auto isolatedContent = ls::readVkBasaltConfiguration(isolatedPath);
        backend.profileSelected(originalProfile);
        backend.separatePowerModesUpdated(true);
        backend.powerModeSelected(1);
        backend.targetFPSUpdated(60);
        backend.powerModeSelected(2);
        backend.targetFPSUpdated(144);
        backend.powerModeSelected(0);
        backend.vkBasaltShaderUpdated(customId + QStringLiteral(":vibrance:custom/Missing"));
        backend.refreshCustomShaders();
        const auto currentPath = backend.getVkBasaltConfigPath().toStdString();
        const auto beforeFailure = ls::readVkBasaltConfiguration(currentPath);
        const auto sidecarPath = directory.filePath("profile-wrapper-settings.json");
        require(QFile::rename(sidecarPath, sidecarPath + QStringLiteral(".backup")) &&
                QDir().mkdir(sidecarPath), "unable to inject sidecar replacement failure");
        backend.refreshCustomShaders();
        require(!backend.getShaderLoadError().isEmpty() &&
                ls::readVkBasaltConfiguration(currentPath) == beforeFailure &&
                backend.getVkBasaltShader() == customId + QStringLiteral(":vibrance:custom/Missing"),
            "Qt shader refresh must restore the reload generation and selection after sidecar failure");
        require(!backend.deleteSelectedCustomShaders() && !backend.getShaderLoadError().isEmpty(),
            "Qt shader deletion must report sidecar failure");
        require(ls::readVkBasaltConfiguration(currentPath) == beforeFailure &&
                backend.getVkBasaltShader() == customId + QStringLiteral(":vibrance:custom/Missing"),
            "failed shader deletion did not restore definitions and selections");
        require(QDir().rmdir(sidecarPath) && QFile::rename(sidecarPath + QStringLiteral(".backup"), sidecarPath),
            "unable to restore sidecar fixture");
        backend.targetFPSUpdated(166);
        QFile configFile(QString::fromStdString(configPath));
        const auto permissions = configFile.permissions();
        require(configFile.setPermissions(QFileDevice::ReadOwner), "unable to inject pending-save failure");
        backend.refreshCustomShaders();
        require(!backend.getShaderLoadError().isEmpty() && ls::readVkBasaltConfiguration(currentPath) == beforeFailure,
            "shader refresh continued after pending settings failed to save");
        require(!backend.deleteSelectedCustomShaders() &&
                ls::readVkBasaltConfiguration(currentPath) == beforeFailure,
            "shader deletion continued after pending settings failed to save");
        require(configFile.setPermissions(permissions), "unable to restore configuration permissions");
        require(backend.deleteSelectedCustomShaders(), "Qt selected shader deletion failed");
        require(backend.getVkBasaltShader() == QStringLiteral("vibrance") &&
                backend.getCustomShaderEffects().size() == 1 &&
                backend.getCustomShaderEffects().front().toMap().value(QStringLiteral("name")) == QStringLiteral("Customother"),
            "deletion did not deselect selected custom entries or retain the unselected shader");
        require(ls::ConfigFile(configPath).profiles().front().target_fps == 166,
            "deletion did not retry and save the pending edit");
        backend.powerModeSelected(1);
        require(backend.getTargetFPS() == 60 && backend.getVkBasaltShader() == QStringLiteral("vibrance"),
            "deletion changed Battery settings or failed to update shared shader selection");
        backend.powerModeSelected(2);
        require(backend.getTargetFPS() == 144 && backend.getVkBasaltShader() == QStringLiteral("vibrance"),
            "deletion changed AC settings or failed to update shared shader selection");
        backend.powerModeSelected(0);
        require(ls::readVkBasaltConfiguration(isolatedPath) == isolatedContent &&
                localShader.open(QIODevice::ReadOnly) && localShader.readAll() == "// synthetic shader fixture\n",
            "deletion modified another profile or the external shader source");
        localShader.close();
        const auto afterDeletion = ls::readVkBasaltConfiguration(currentPath);
        require(backend.deleteSelectedCustomShaders() && ls::readVkBasaltConfiguration(currentPath) == afterDeletion,
            "deletion without selected custom shaders rewrote the profile");
        // Remove the remaining fixture and restore the selection used by the reopen test.
        backend.vkBasaltShaderUpdated(QStringLiteral("custom/Customother"));
        require(backend.deleteSelectedCustomShaders() && backend.getVkBasaltShader() == QStringLiteral("none"),
            "deleting the last custom effect did not select Off");
        require(backend.addCustomShader(shaderPath), "deleted shader could not be registered again");
        backend.vkBasaltShaderUpdated(customId + QStringLiteral(":vibrance"));
        backend.refreshCustomShaders();
        const auto timestamp = std::filesystem::last_write_time(configPath);
        QTimer::singleShot(700, &events, &QEventLoop::quit);
        events.exec();
        require(std::filesystem::last_write_time(configPath) == timestamp,
            "Idle UI rewrote the configuration");
        backend.targetFPSUpdated(165);
        for (const auto* quality : {"quality", "performance", "balanced"}) {
            backend.hdrQualityUpdated(QString::fromLatin1(quality));
            require(backend.getHdrQuality() == QString::fromLatin1(quality),
                "Qt must accept every HDR quality choice");
        }
        backend.hdrQualityUpdated(QStringLiteral("balanced"));
        backend.hdrQualityUpdated(QStringLiteral("invalid"));
        require(backend.getHdrQuality() == QStringLiteral("balanced"), "Invalid HDR quality must preserve the selection");
        require(backend.setProperty("enable_hdr", false), "Enable HDR property must accept Off");
        backend.disableSteamOverlayUpdated(true);
        backend.forceAlsaAudioUpdated(true);
        // No event loop: closing before the debounce must still save both files.
    }
    {
        mako::ui::Backend reopened;
        require(!reopened.getEnableHdr(),
            "A saved disable_hdr_exposure=true must appear as Enable HDR Off");
        require(reopened.getHdrQuality() == QStringLiteral("balanced"),
            "Qt must preserve the saved Balanced HDR choice");
        require(reopened.getVkBasaltShader() == QStringLiteral("custom/Customcustomtone:vibrance") &&
                reopened.getCustomShaderEffects().size() == 1,
            "Qt custom selection and catalog did not survive reopening");
    }
    require(ls::ConfigFile(configPath).profiles().front().target_fps == 165,
        "Closing the UI lost the pending profile edit");
    require(ls::LaunchConfigFile(launchPath).settings().disable_hdr_exposure,
        "Closing the UI lost the pending HDR opt-out");
    require(ls::LaunchConfigFile(launchPath).settings().disable_steam_overlay,
        "Closing the UI lost the pending overlay edit");
    require(ls::LaunchConfigFile(launchPath).settings().force_alsa_audio,
        "Closing the UI lost the pending launcher edit");
    QFile invalid(QString::fromStdString(configPath));
    require(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate), "Invalid fixture could not be written");
    invalid.write("version = [broken");
    invalid.close();
    QFile backup(QString::fromStdString(configPath + ".old"));
    require(backup.open(QIODevice::WriteOnly), "Backup fixture could not be written");
    backup.write("previous backup");
    backup.close();
    bool refused = false;
    try { mako::ui::Backend backend; }
    catch (const std::exception&) { refused = true; }
    require(refused, "UI accepted an invalid configuration without preserving its backup");
    require(invalid.open(QIODevice::ReadOnly) && invalid.readAll() == "version = [broken",
        "UI changed the invalid configuration after backup failed");
    require(backup.open(QIODevice::ReadOnly) && backup.readAll() == "previous backup",
        "UI overwrote an earlier configuration backup");
    if (previousConfig.isNull()) qunsetenv("MAKO_CONFIG");
    else qputenv("MAKO_CONFIG", previousConfig);
    if (previousLaunch.isNull()) qunsetenv("MAKO_LAUNCH_CONFIG");
    else qputenv("MAKO_LAUNCH_CONFIG", previousLaunch);
    if (previousConfigHome.isNull()) qunsetenv("XDG_CONFIG_HOME");
    else qputenv("XDG_CONFIG_HOME", previousConfigHome);
    if (previousShaderDirectory.isNull()) qunsetenv("MAKO_VKBASALT_SHADER_DIR");
    else qputenv("MAKO_VKBASALT_SHADER_DIR", previousShaderDirectory);
}

void test_decky_shader_profile_round_trip_and_owned_deletion() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary configuration directory failed");
    const auto configPath = directory.filePath("conf.toml").toStdString();
    const auto launchPath = directory.filePath("launcher.conf").toStdString();
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto previousShaderDirectory = qgetenv("MAKO_VKBASALT_SHADER_DIR");
    qputenv("MAKO_CONFIG", QByteArray::fromStdString(configPath));
    qputenv("MAKO_LAUNCH_CONFIG", QByteArray::fromStdString(launchPath));
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("MAKO_VKBASALT_SHADER_DIR",
        QByteArray(MAKO_UI_VKBASALT_SHADER_SOURCE_DIR));

    ls::ConfigFile config;
    config.profiles().front().name = "decky-game";
    ls::GameConf unrelatedProfile;
    unrelatedProfile.name = "other-game";
    config.profiles().push_back(unrelatedProfile);
    config.write(configPath);

    QFile sidecar(directory.filePath("profile-wrapper-settings.json"));
    require(sidecar.open(QIODevice::WriteOnly),
        "Decky wrapper settings fixture could not be written");
    sidecar.write(R"JSON({
  "version": 1,
  "profiles": {
    "decky-game": {
      "external_vulkan_layer": "vkbasalt",
      "vkbasalt_sharpening": "dls",
      "vkbasalt_sharpness": 0.65,
      "vkbasalt_dls_denoise": 0.35,
      "vkbasalt_antialiasing": "smaa",
      "vkbasalt_shader": "technicolor2:clarity",
      "future_setting": "keep"
    },
    "other-game": {
      "external_vulkan_layer": "",
      "vkbasalt_sharpening": "cas",
      "vkbasalt_sharpness": 0.5,
      "vkbasalt_dls_denoise": 0.2,
      "vkbasalt_antialiasing": "none",
      "vkbasalt_shader": "none",
      "unrelated": true
    }
  }
})JSON");
    sidecar.close();

    QFile metadata(directory.filePath("profile-metadata.json"));
    require(metadata.open(QIODevice::WriteOnly),
        "Decky profile metadata fixture could not be written");
    metadata.write(R"JSON({
  "version": 1,
  "profiles": {
    "decky-game": {"display_name": "Decky Game", "kind": "steam", "steam_app_id": "111", "captured_processes": []},
    "other-game": {"display_name": "Other Game", "kind": "steam", "steam_app_id": "222", "captured_processes": []}
  }
})JSON");
    metadata.close();

    const auto profileDirectory = directory.filePath("vkbasalt");
    require(QDir().mkpath(profileDirectory),
        "profile shader directory fixture could not be created");
    QFile ownedShader(profileDirectory + QStringLiteral("/steam-111.conf"));
    require(ownedShader.open(QIODevice::WriteOnly),
        "owned shader fixture could not be written");
    ownedShader.write("effects = custom\n");
    ownedShader.close();
    QFile unrelatedShader(profileDirectory + QStringLiteral("/steam-222.conf"));
    require(unrelatedShader.open(QIODevice::WriteOnly),
        "unrelated shader fixture could not be written");
    unrelatedShader.write("effects = unrelated\n");
    unrelatedShader.close();
    const auto globalShaderPath = directory.filePath("vkBasalt/vkBasalt.conf");
    require(QDir().mkpath(QFileInfo(globalShaderPath).path()),
        "global shader directory fixture could not be created");
    QFile globalShader(globalShaderPath);
    require(globalShader.open(QIODevice::WriteOnly),
        "global shader fixture could not be written");
    globalShader.write("effects = global\n");
    globalShader.close();

    {
        mako::ui::Backend backend;
        require(backend.getEnableVkBasalt() &&
                backend.getVkBasaltSharpening() == QStringLiteral("dls") &&
                std::abs(backend.getVkBasaltSharpness() - 0.65F) < 0.001F &&
                std::abs(backend.getVkBasaltDlsDenoise() - 0.35F) < 0.001F &&
                backend.getVkBasaltAntialiasing() == QStringLiteral("smaa") &&
                backend.getVkBasaltShader() == QStringLiteral("technicolor2:clarity"),
            "Qt did not load the selected profile's existing Decky shader settings");
        require(backend.getVkBasaltConfigPath().endsWith(
                    QStringLiteral("/steam-111.conf")),
            "Qt did not reuse Decky's Steam-aware shader config identity");
        backend.deleteProfile();
        QEventLoop events;
        QTimer::singleShot(700, &events, &QEventLoop::quit);
        events.exec();
    }

    require(!QFileInfo::exists(profileDirectory + QStringLiteral("/steam-111.conf")),
        "deleting the main profile left its attached shader config behind");
    require(QFileInfo::exists(profileDirectory + QStringLiteral("/steam-222.conf")) &&
            QFileInfo::exists(globalShaderPath),
        "deleting one profile removed an unrelated or global shader config");

    require(sidecar.open(QIODevice::ReadOnly),
        "updated Decky wrapper settings could not be read");
    const auto storedSettings = QJsonDocument::fromJson(sidecar.readAll())
        .object().value(QStringLiteral("profiles")).toObject();
    require(!storedSettings.contains(QStringLiteral("decky-game")) &&
            storedSettings.value(QStringLiteral("other-game")).toObject()
                .value(QStringLiteral("unrelated")).toBool(),
        "profile deletion changed Decky settings outside the attached profile");
    sidecar.close();
    require(metadata.open(QIODevice::ReadOnly),
        "updated Decky metadata could not be read");
    const auto storedMetadata = QJsonDocument::fromJson(metadata.readAll())
        .object().value(QStringLiteral("profiles")).toObject();
    require(!storedMetadata.contains(QStringLiteral("decky-game")) &&
            storedMetadata.contains(QStringLiteral("other-game")),
        "profile deletion changed Decky metadata outside the attached profile");

    if (previousConfig.isNull()) qunsetenv("MAKO_CONFIG");
    else qputenv("MAKO_CONFIG", previousConfig);
    if (previousLaunch.isNull()) qunsetenv("MAKO_LAUNCH_CONFIG");
    else qputenv("MAKO_LAUNCH_CONFIG", previousLaunch);
    if (previousConfigHome.isNull()) qunsetenv("XDG_CONFIG_HOME");
    else qputenv("XDG_CONFIG_HOME", previousConfigHome);
    if (previousShaderDirectory.isNull()) qunsetenv("MAKO_VKBASALT_SHADER_DIR");
    else qputenv("MAKO_VKBASALT_SHADER_DIR", previousShaderDirectory);
}

void test_saved_editor_selection() {
    QTemporaryDir directory;
    require(directory.isValid(), "editor selection fixture failed");
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto config = directory.filePath("conf.toml");
    qputenv("MAKO_CONFIG", config.toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launcher.conf").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    for (const auto& [selection, expected] : {
            std::pair{"", "mako"},
            std::pair{"# decky-current-profile = \"stream-quality\"\n", "stream-quality"},
            std::pair{"current_profile = \"mako\"\n# decky-current-profile = \"stream-quality\"\n", "mako"},
            std::pair{"current_profile = \"deleted\"\n# decky-current-profile = \"stream-quality\"\n", "stream-quality"}}) {
        QFile file(config);
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write editor selection fixture");
        file.write(QByteArray("version=2\n") + selection +
            "[[profile]]\nname=\"stream-quality\"\n[[profile]]\nname=\"mako\"\n");
        file.close();
        {
            mako::ui::Backend backend;
            require(backend.calculateProfileListModel()->stringList().at(backend.getProfileIndex()) ==
                    QString::fromUtf8(expected), "Qt selection differs from the native stream selection");
            backend.profileSelected(backend.getProfileIndex());
        }
        require(ls::ConfigFile(config.toStdString()).current_profile == expected,
            "Qt did not persist the ordinary editor selection");
    }
    {
        mako::ui::Backend backend;
        const auto flush = []() {
            QEventLoop events;
            QTimer::singleShot(700, &events, &QEventLoop::quit);
            events.exec();
        };
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltSharpeningUpdated(QStringLiteral("cas"));
        backend.vkBasaltSharpnessUpdated(0.73F);
        flush();
        const auto sourcePath = backend.getVkBasaltConfigPath();
        QFile source(sourcePath);
        require(source.open(QIODevice::ReadOnly), "cannot read source shaders");
        const auto sourceContent = source.readAll();
        const auto cachePath = directory.filePath("vkbasalt/current-profile.conf");
        QFile cache(cachePath);
        require(cache.open(QIODevice::ReadOnly) && cache.readAll() == sourceContent,
            "Qt did not seed selection-following shader content");
        cache.close();
        backend.createProfile(QStringLiteral("Future streams"));
        flush();
        require(cache.open(QIODevice::ReadOnly) && cache.readAll() == sourceContent,
            "Creating a profile did not copy selection-following shaders");
        cache.close();
        backend.enableVkBasaltUpdated(false);
        flush();
        require(cache.open(QIODevice::ReadOnly) && cache.readAll() == "effects = none\n",
            "Disabling shaders retained the previous effects");
        cache.close();
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltSharpeningUpdated(QStringLiteral("cas"));
        backend.vkBasaltSharpnessUpdated(0.91F);
        flush();
        require(cache.open(QIODevice::ReadOnly) && cache.readAll().contains("casSharpness = 0.91"),
            "The stream shader filename did not follow the new profile's edits");
        cache.close();
        backend.refreshCustomShaders();
        require(cache.open(QIODevice::ReadOnly) && cache.readAll().contains("makoReloadGeneration = 1"),
            "Explicit shader reload did not reach the selection-following stream");
        source.close();
        require(source.open(QIODevice::ReadOnly) && source.readAll() == sourceContent,
            "Changing selected shaders modified the old ordinary profile");
    }
    for (const auto& entry : {std::pair{"MAKO_CONFIG", previousConfig},
            std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch}, std::pair{"XDG_CONFIG_HOME", previousConfigHome}}) {
        if (entry.second.isNull()) qunsetenv(entry.first);
        else qputenv(entry.first, entry.second);
    }
}

void test_remote_play_shared_command_and_profile_preservation() {
    QTemporaryDir directory;
    require(directory.isValid(), "Remote Play fixture failed");
    const auto previousHome = qgetenv("HOME");
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const auto config = directory.filePath(".config/mako-render/conf.toml");
    const auto clientPath = directory.filePath(".local/share/Steam/ubuntu12_64/streaming_client");
    require(QDir().mkpath(QFileInfo(clientPath).absolutePath()), "Remote Play Steam tree failed");
    QByteArray original(24, '\0');
    original.replace(0, 6, QByteArray("\x7f" "ELF\x02\x01", 6));
    original[18] = '\x3e';
    QFile client(clientPath);
    require(client.open(QIODevice::WriteOnly) && client.write(original) == original.size(), "Remote Play original fixture failed");
    client.close();
    require(client.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner), "Remote Play permissions failed");
    // Stage the shared command at an installed prefix. Native SDL behavior
    // has its own CTest; this test validates Qt's async command and recovery.
    const auto source = QFileInfo(QStringLiteral(MAKO_UI_REMOTE_PLAY_HELPER)).absolutePath();
    const auto bin = directory.filePath(".local/bin");
    const auto owner = directory.filePath(".local/share/mako-render/mako_remote_play");
    require(QDir().mkpath(bin) && QDir().mkpath(owner), "Remote Play package fixture failed");
    const auto command = bin + "/mako-remote-play";
    require(QFile::copy(QStringLiteral(MAKO_UI_REMOTE_PLAY_HELPER), command) &&
        QFile::copy(source + "/mako-launch", bin + "/mako-launch"), "Remote Play commands could not be staged");
    for (const auto& module : {"__init__.py", "remote_play_core.py", "remote_play_launch.py", "managed_files.py"})
        require(QFile::copy(source + "/mako_remote_play/" + module, owner + "/" + module), "Remote Play owner could not be staged");
    auto helperBytes = original;
    helperBytes[16] = '\x03';
    helperBytes.append("MAKO_REMOTE_PLAY_SDR_HELPER_V1");
    QFile sdrHelper(owner + "/libmako-remote-play-sdr.so");
    require(sdrHelper.open(QIODevice::WriteOnly) && sdrHelper.write(helperBytes) == helperBytes.size(), "Remote Play SDR fixture failed");
    sdrHelper.close();
    qputenv("HOME", directory.path().toUtf8());
    qputenv("MAKO_CONFIG", config.toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launcher.conf").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.filePath(".config").toUtf8());
    {
        mako::ui::Backend backend("/proc", command);
        const auto wait = [&backend] {
            QElapsedTimer elapsed;
            elapsed.start();
            while (backend.remotePlayBusy() && elapsed.elapsed() < 5000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(1);
            }
            QCoreApplication::processEvents();
            require(!backend.remotePlayBusy(), "Remote Play command did not finish asynchronously");
        };
        backend.refreshRemotePlay();
        wait();
        require(!backend.remotePlayInstalled() && backend.remotePlayAvailable(), "Remote Play was implicitly installed or unavailable");
        backend.createProfile("stream-quality");
        backend.separatePowerModesUpdated(true);
        backend.powerModeSelected(1);
        backend.targetFPSUpdated(72);
        backend.setRemotePlayOverride(true);
        wait();
        if (!backend.remotePlayInstalled()) {
            require(backend.remotePlayMessage().contains("Python 3.11"), "shared Remote Play installation failed");
        } else {
            require(backend.remotePlayMessage().isEmpty(), "successful Remote Play install reported an error");
            backend.setRemotePlayOverride(false);
            wait();
            require(!backend.remotePlayManaged(), "Qt did not restore the shared Steam override");
            const auto saved = ls::ConfigFile(config.toStdString());
            require(saved.current_profile == "stream-quality", "override removal lost the editor selection");
            const auto found = std::find_if(saved.profiles().begin(), saved.profiles().end(), [](const auto& profile) {
                return profile.name == "stream-quality";
            });
            require(found != saved.profiles().end() && found->power_profiles.size() == 2 &&
                    found->power_profiles.front().target_fps == 72, "override removal reset existing power settings");
        }
        require(client.open(QIODevice::ReadOnly) && client.readAll() == original, "Qt did not preserve Steam's original client");
        client.close();
    }
    for (const auto& entry : {std::pair{"HOME", previousHome}, std::pair{"MAKO_CONFIG", previousConfig},
            std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch}, std::pair{"XDG_CONFIG_HOME", previousConfigHome}}) {
        if (entry.second.isNull()) qunsetenv(entry.first);
        else qputenv(entry.first, entry.second);
    }
}

void test_profile_management_alignment() {
    QTemporaryDir directory;
    require(directory.isValid(), "profile management fixture failed");
    const auto previousConfig = qgetenv("MAKO_CONFIG");
    const auto previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG");
    const auto previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("MAKO_CONFIG", directory.filePath("conf.toml").toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launcher.conf").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    const auto write = [](const QString& path, const QByteArray& content) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "cannot write profile fixture");
        require(file.write(content) == content.size(), "incomplete profile fixture");
    };
    const auto read = [](const QString& path) {
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "cannot read profile fixture");
        return file.readAll();
    };
    const auto configPath = directory.filePath("conf.toml");
    const auto wrapperPath = directory.filePath("profile-wrapper-settings.json");
    const auto metadataPath = directory.filePath("profile-metadata.json");
    const auto cachePath = directory.filePath("vkbasalt/current-profile.conf");
    const auto sourcePath = directory.filePath("vkbasalt/steam-111.conf");
    require(QDir().mkpath(directory.filePath("vkbasalt")), "cannot prepare profile shaders");
    ls::ConfigFile config;
    ls::GameConf source;
    source.name = "game";
    source.active_in = {"Game.exe", "ManualAlias"};
    source.target_fps = 144;
    auto handheld = source;
    handheld.target_fps = 60;
    auto docked = source;
    docked.target_fps = 120;
    source.power_profiles = {handheld, docked};
    ls::GameConf defaults;
    defaults.name = "mako";
    // Default is deliberately not the first entry.
    config.profiles() = {source, defaults};
    config.current_profile = "game";
    config.write(configPath.toStdString());
    write(wrapperPath, R"JSON({"version":1,"profiles":{"game":{"external_vulkan_layer":"vkbasalt","vkbasalt_shader":"custom/Tone","vkbasalt_manage_custom_shaders":true,"force_alsa_audio":true,"future_setting":"keep"}}})JSON");
    write(metadataPath, R"JSON({"version":1,"profiles":{"game":{"display_name":"Game","kind":"game","steam_app_id":"111","captured_processes":["Game.exe"]},"Future streams":{"steam_app_id":"111"},"Saved streams":{"steam_app_id":"999"}}})JSON");
    write(sourcePath, "effects = Tone\nTone = /shaders/Tone.fx\nToneStrength = 0.42\n# advanced settings\n");
    const auto originalSource = read(sourcePath);
    {
        mako::ui::Backend backend;
        require_property("can_manage_profile", "bool", false, false);
        require_property("profile_operation_error", "QString", false, false);
        require(backend.canManageProfile() && backend.getProfileIndex() == 0,
            "saved game selection was not restored");
        for (const auto& name : {QStringLiteral(" "), QStringLiteral("global"),
                QStringLiteral("PROFILE"), QStringLiteral("bad/name"), QStringLiteral("bad\nname")})
            require(!backend.createProfile(name) && backend.profileOperationError() == "invalid_name",
                "Qt accepted an invalid profile name");
        require(!backend.createProfile("game") && backend.profileOperationError() == "duplicate_name",
            "Qt accepted a duplicate profile");
        backend.targetFPSUpdated(165); // Creation must drain the pending edit.
        require(backend.createProfile("  Future streams  "), "offline profile copy failed");
        const auto saved = ls::ConfigFile(configPath.toStdString());
        require(saved.current_profile == "Future streams" && backend.getProfileIndex() == 2 &&
                backend.getTargetFPS() == 165 && saved.profiles().back().active_in == source.active_in &&
                saved.profiles().back().power_profiles.size() == 2 &&
                saved.profiles().back().power_profiles.front().target_fps == 60 &&
                saved.profiles().back().power_profiles.back().target_fps == 120,
            "profile copy lost pending edits, identity, power settings, or saved selection");
        const auto clonePath = backend.getVkBasaltConfigPath();
        require(clonePath != sourcePath && read(clonePath) == read(sourcePath) &&
                read(cachePath) == read(clonePath) && backend.getCustomShaderEffects().size() == 1,
            "profile copy did not preserve independent custom shader content");
        require(read(sourcePath) == originalSource,
            "creating a profile changed the source shader file");
        auto wrappers = QJsonDocument::fromJson(read(wrapperPath)).object().value("profiles").toObject();
        require(wrappers.value("Future streams").toObject().value("force_alsa_audio").toBool() &&
                wrappers.value("Future streams").toObject().value("future_setting").toString() == "keep",
            "profile copy discarded Decky wrapper fields");
        const auto metadata = QJsonDocument::fromJson(read(metadataPath)).object().value("profiles").toObject();
        require(metadata.value("Future streams").toObject().value("kind") == "process" &&
                metadata.value("Future streams").toObject().value("steam_app_id").isNull() &&
                metadata.value("Future streams").toObject().value("captured_processes").toArray().isEmpty() &&
                metadata.value("game").toObject().value("steam_app_id") == "111",
            "manual copy inherited a Steam association or changed the source association");
        require(!backend.renameProfile("game") && backend.getProfileIndex() == 2,
            "duplicate rename changed the selection");

        const auto beforeConfig = read(configPath);
        const auto beforeWrapper = read(wrapperPath);
        const auto beforeMetadata = read(metadataPath);
        const auto beforeShader = read(clonePath);
        const auto beforeCache = read(cachePath);
        const auto beforeFiles = QDir(directory.filePath("vkbasalt")).entryList(QDir::Files);
        require(QFile::setPermissions(configPath, QFileDevice::ReadOwner), "cannot block native profile writes");
        require(!backend.createProfile("Failed copy") && !backend.renameProfile("Failed rename") &&
                !backend.deleteProfile(), "profile operation succeeded after a failed native write");
        backend.profileSelected(1);
        require(backend.getProfileIndex() == 2 && backend.calculateProfileListModel()->rowCount() == 3 &&
                backend.profileOperationError() == "save_failed" &&
                read(configPath) == beforeConfig && read(wrapperPath) == beforeWrapper &&
                read(metadataPath) == beforeMetadata && read(clonePath) == beforeShader &&
                read(cachePath) == beforeCache &&
                QDir(directory.filePath("vkbasalt")).entryList(QDir::Files) == beforeFiles,
            "failed profile operations changed selection or left partial profile files");
        require(QFile::setPermissions(configPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner),
            "cannot unblock native profile writes");
        require(backend.renameProfile("Saved streams"), "profile rename failed after recovery");
        const auto renamedPath = backend.getVkBasaltConfigPath();
        require(!QFileInfo::exists(clonePath) && read(renamedPath) == beforeShader &&
                ls::ConfigFile(configPath.toStdString()).current_profile == "Saved streams",
            "rename did not move the shader content and saved selection");
        require(backend.deleteProfile() && backend.getProfileIndex() == 1 &&
                !backend.canManageProfile() && !QFileInfo::exists(renamedPath) &&
                QFileInfo::exists(sourcePath) && ls::ConfigFile(configPath.toStdString()).current_profile == "mako",
            "deletion did not preserve the source and fall back to Default by name");
        require(!backend.renameProfile("Renamed Default") && !backend.deleteProfile() &&
                backend.profileOperationError() == "default_protected",
            "Qt permitted renaming or deleting Default");
        backend.profileSelected(-1);
        backend.profileSelected(99);
        require(backend.getProfileIndex() == 1, "invalid selection replaced Default");
        require(backend.createProfile("Reusable offline"), "cannot create from Default without a game");
        backend.targetFPSUpdated(177);
        require(QFile::setPermissions(configPath, QFileDevice::ReadOwner), "cannot block a pending save");
        require(!backend.createProfile("After pending save") && backend.getTargetFPS() == 177 &&
                backend.calculateProfileListModel()->rowCount() == 3,
            "failed pending save created a profile or discarded the edit");
        require(QFile::setPermissions(configPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner),
            "cannot restore pending-save access");
        require(backend.createProfile("After pending save") && backend.getTargetFPS() == 177,
            "creation did not retry and copy the pending edit after recovery");
    }
    {
        mako::ui::Backend backend;
        require(backend.calculateProfileListModel()->stringList().at(backend.getProfileIndex()) == "After pending save",
            "new offline profile selection did not survive reopening Qt");
    }
    for (const auto& entry : {std::pair{"MAKO_CONFIG", previousConfig},
            std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch}, std::pair{"XDG_CONFIG_HOME", previousConfigHome}}) {
        if (entry.second.isNull()) qunsetenv(entry.first);
        else qputenv(entry.first, entry.second);
    }
}

void test_shader_parameter_editor_persistence() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary shader editor directory failed");
    const auto previousConfig = qgetenv("MAKO_CONFIG"), previousLaunch = qgetenv("MAKO_LAUNCH_CONFIG"), previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("MAKO_CONFIG", directory.filePath("conf.toml").toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launcher.conf").toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    const auto helperPath = directory.filePath("shader-tool");
    QFile helper(helperPath);
    require(helper.open(QIODevice::WriteOnly), "cannot create shader helper fixture");
    helper.write(R"SH(#!/bin/sh
cat <<'JSON'
{"schema":1,"effects":[{"id":"Tone","name":"Tone","path":"/tone.fx","error":null,"parameters":[{"key":"makoShader_546f6e65_537472656e677468_0","label":"Strength","description":"","category":"","type":"float","default":0.25,"value":0.5,"min":0,"max":1,"step":0.01,"items":[]},{"key":"makoShader_546f6e65_53656564_0","label":"Seed","description":"","category":"","type":"uint","default":0,"value":0,"min":1,"max":null,"step":null,"items":[]}]}]}
JSON
)SH");
    helper.close();
    require(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner), "cannot make shader helper executable");
    {
        mako::ui::Backend backend("/proc", {}, helperPath);
        backend.enableVkBasaltUpdated(true);
        backend.vkBasaltShaderUpdated(QStringLiteral("vibrance"));
        const auto load = [&] {
            backend.loadShaderParameters();
            QElapsedTimer timeout; timeout.start();
            while (backend.shaderParametersLoading() && timeout.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
            require(!backend.shaderParametersLoading() && backend.shaderParameterError().isEmpty() && backend.shaderParameterEffects().size() == 1, "shader metadata did not load asynchronously");
        };
        load();
        const auto path = backend.getVkBasaltConfigPath().toStdString();
        const auto key = QStringLiteral("makoShader_546f6e65_537472656e677468_0");
        require(backend.setShaderParameters({{key, 0.75}}), "shader value was not saved");
        require(backend.setShaderParameters({{QStringLiteral("makoShader_546f6e65_53656564_0"), 4294967295.0}}), "large unsigned shader value was not saved");
        const auto saved = ls::readVkBasaltConfiguration(path);
        require(saved.find("makoShader_546f6e65_53656564_0 = 4294967295") != std::string::npos, "integer serialization used scientific notation or lost precision");
        require(saved.find("makoShader_546f6e65_537472656e677468_0 = 0.75") != std::string::npos, "saved shader value is missing");
        require(!backend.setShaderParameters({{key, 2.0}}) && ls::readVkBasaltConfiguration(path) == saved, "invalid shader value changed the profile");
        require(!backend.setShaderParameters({{QStringLiteral("effects"), 1.0}}), "unknown parameter accepted");
        // An external edit must invalidate the metadata snapshot before any write.
        ls::writeVkBasaltConfiguration(path, saved + "# changed externally\n");
        require(!backend.setShaderParameters({{key, 0.2}}), "stale shader editor overwrote external changes");
        load();
        require(backend.setShaderParameters({{key, QVariant()}}) && ls::readVkBasaltConfiguration(path).find(" = default") != std::string::npos, "reset did not use the reflected default");
        require(backend.setShaderParameters({{QStringLiteral("makoShader_546f6e65_53656564_0"), QVariant()}}), "reset refused the authored default outside its UI range");
        backend.loadShaderParameters();
        ls::writeVkBasaltConfiguration(path, ls::readVkBasaltConfiguration(path) + "# edit during discovery\n");
        QElapsedTimer discoveryTimeout; discoveryTimeout.start();
        while (backend.shaderParametersLoading() && discoveryTimeout.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(!backend.shaderParametersLoading() && !backend.shaderParameterError().isEmpty() && backend.shaderParameterEffects().isEmpty(), "discovery displayed controls from a changed configuration");
        backend.loadShaderParameters();
        require(backend.createProfile(QStringLiteral("Other shader profile")), "cannot copy shader profile");
        require(!backend.setShaderParameters({{key, 0.3}}), "shader editor followed a different selected profile");
        load(); // A newly opened editor must supersede discovery for the previous profile.
        require(backend.setShaderParameters({{key, 0.4}}), "reopened editor did not load the new profile");
        const auto goodMetadata = QJsonObject{{"schema", 1}, {"effects", QJsonArray::fromVariantList(backend.shaderParameterEffects())}};
        const auto publish = [&](const QJsonObject& metadata) {
            QFile fixture(helperPath);
            require(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot replace shader helper fixture");
            fixture.write("#!/bin/sh\ncat <<'JSON'\n");
            fixture.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
            fixture.write("\nJSON\n");
        };
        const auto withParameter = [&](const QJsonObject& parameter) {
            auto metadata = goodMetadata;
            auto effects = metadata["effects"].toArray();
            auto effect = effects[0].toObject();
            effect["parameters"] = QJsonArray{parameter};
            effects[0] = effect; metadata["effects"] = effects;
            return metadata;
        };
        const auto parameter = goodMetadata["effects"].toArray()[0].toObject()["parameters"].toArray()[0].toObject();
        for (const auto& mutation : {
                QJsonObject{{"key", "makoShader_41_537472656e677468_0"}},
                QJsonObject{{"type", "double"}}, QJsonObject{{"value", true}},
                QJsonObject{{"default", "0.5"}}, QJsonObject{{"label", QJsonArray{}}},
                QJsonObject{{"items", QJsonObject{}}}, QJsonObject{{"resettable", "false"}}}) {
            auto bad = parameter;
            for (auto it = mutation.begin(); it != mutation.end(); ++it) bad[it.key()] = it.value();
            publish(withParameter(bad)); backend.loadShaderParameters();
            QElapsedTimer timer; timer.start();
            while (backend.shaderParametersLoading() && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
            require(!backend.shaderParametersLoading() && !backend.shaderParameterError().isEmpty() && backend.shaderParameterEffects().isEmpty(), "malformed shader metadata reached the Qt controls");
            require(!backend.setShaderParameters({{key, .5}}), "invalid metadata retained a writable parameter cache");
        }
        auto duplicate = goodMetadata;
        auto effects = duplicate["effects"].toArray();
        effects.append(effects[0]); duplicate["effects"] = effects;
        publish(duplicate); backend.loadShaderParameters();
        QElapsedTimer timer; timer.start();
        while (backend.shaderParametersLoading() && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        require(!backend.shaderParameterError().isEmpty() && backend.shaderParameterEffects().isEmpty(), "duplicate effect identity was accepted");
        auto keep = parameter;
        keep["resettable"] = false; keep["min"] = .100000001; keep["max"] = .899999976;
        publish(withParameter(keep)); load();
        require(backend.setShaderParameters({{key, .1}}) && backend.setShaderParameters({{key, .9}}), "valid FP32 boundary was rejected");
        require(!backend.setShaderParameters({{key, .901}}) && !backend.setShaderParameters({{key, .099}}), "invalid boundary value was accepted");
        require(!backend.setShaderParameters({{key, true}}) && !backend.setShaderParameters({{key, QStringLiteral("0.5")}}), "Qt coerced a boolean or string into a numeric command");
        require(!backend.setShaderParameters({{key, QVariant()}}), "Qt ignored authored no-reset restriction");
        auto action = parameter;
        action["type"] = "bool"; action["value"] = 0; action["default"] = 0;
        action["presentation"] = QJsonObject{{"widget", "button"}, {"group", key}, {"label", "Capture"}, {"component", 0}, {"count", 1}};
        publish(withParameter(action)); load();
        require(backend.shaderParameterEffects()[0].toMap()["parameters"].toList().isEmpty(), "one-frame action became a persistent Qt toggle");
    }
    for (const auto& entry : {std::pair{"MAKO_CONFIG", previousConfig}, std::pair{"MAKO_LAUNCH_CONFIG", previousLaunch}, std::pair{"XDG_CONFIG_HOME", previousConfigHome}}) {
        if (entry.second.isNull()) qunsetenv(entry.first); else qputenv(entry.first, entry.second);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication application(argc, argv);
    try {
        test_installed_launcher_selection();
        test_installed_launcher_wine_environment();
        test_mangohud_profile_and_availability();
        test_scaling_properties();
        test_lossless_scaling_is_optional();
        test_lossless_model_inspection();
        test_power_profile_editor();
        test_multiplier_limits();
        test_fractional_adaptive_preset();
        test_feature_group_order_and_ownership();
        test_shader_controls();
        test_shader_parameter_editor_persistence();
        test_compact_restart_markers();
        test_save_lifetime();
        test_decky_shader_profile_round_trip_and_owned_deletion();
        test_saved_editor_selection();
        test_remote_play_shared_command_and_profile_preservation();
        test_profile_management_alignment();
    } catch (const std::exception& error) {
        std::cerr << "mako-ui backend contract test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
