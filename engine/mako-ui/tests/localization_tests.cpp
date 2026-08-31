/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "localization.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QByteArray catalog_data() {
    QFile file(QString::fromUtf8(MAKO_UI_TRANSLATIONS_FILE));
    require(file.open(QIODevice::ReadOnly), "translation catalog could not be opened");
    return file.readAll();
}

void test_locale_selection(const QByteArray& catalog, const QString& temporary_path) {
    mako::ui::Localization brazil(catalog, temporary_path + "/brazil.ini", QLocale("pt_BR"));
    require(brazil.language() == QStringLiteral("pt-BR"), "Brazilian Portuguese was not detected");
    require(brazil.language_names().size() == 8, "expected eight selectable languages");
    require(brazil.strings().value(QStringLiteral("language")).toString() == QStringLiteral("Idioma"),
            "Brazilian Portuguese catalog was not selected");

    mako::ui::Localization portugal(catalog, temporary_path + "/portugal.ini", QLocale("pt_PT"));
    require(portugal.language() == QStringLiteral("pt-PT"), "European Portuguese was not detected");

    mako::ui::Localization spanish(catalog, temporary_path + "/spanish.ini", QLocale("es_ES"));
    require(spanish.language() == QStringLiteral("es"), "Spanish was not detected");

    mako::ui::Localization korean(catalog, temporary_path + "/korean.ini", QLocale("ko_KR"));
    require(korean.language() == QStringLiteral("ko"), "Korean was not detected");

    mako::ui::Localization japanese(catalog, temporary_path + "/japanese.ini", QLocale("ja_JP"));
    require(japanese.language() == QStringLiteral("ja"), "Japanese was not detected");

    mako::ui::Localization ukrainian(catalog, temporary_path + "/ukrainian.ini", QLocale("uk_UA"));
    require(ukrainian.language() == QStringLiteral("uk"), "Ukrainian was not detected");
    require(ukrainian.strings().value(QStringLiteral("language")).toString() == QStringLiteral("Мова"),
            "Ukrainian catalog was not selected");

    mako::ui::Localization chinese(catalog, temporary_path + "/chinese.ini", QLocale("zh_CN"));
    require(chinese.language() == QStringLiteral("zh"), "Simplified Chinese was not detected");

    mako::ui::Localization fallback(catalog, temporary_path + "/fallback.ini", QLocale("de_DE"));
    require(fallback.language() == QStringLiteral("en"), "unsupported locales must use English");
}

void test_persistence(const QByteArray& catalog, const QString& settings_file) {
    {
        mako::ui::Localization localization(catalog, settings_file, QLocale("en_US"));
        localization.set_language_index(3);
        require(localization.language() == QStringLiteral("es"), "language index did not select Spanish");
        require(localization.language_index() == 3, "selected language index was not retained");
        localization.set_language(QStringLiteral("invalid"));
        require(localization.language() == QStringLiteral("es"), "invalid language changed the selection");
    }

    mako::ui::Localization restored(catalog, settings_file, QLocale("en_US"));
    require(restored.language() == QStringLiteral("es"), "language selection was not persisted");
}

void test_invalid_persisted_language(const QByteArray& catalog, const QString& settings_file) {
    {
        QSettings settings(settings_file, QSettings::IniFormat);
        settings.setValue(QStringLiteral("interface/language"), QStringLiteral("unknown"));
    }

    mako::ui::Localization localization(catalog, settings_file, QLocale("pt_BR"));
    require(localization.language() == QStringLiteral("pt-BR"),
            "invalid persisted language did not fall back to the system locale");
}

void test_catalog_validation(const QByteArray& catalog, const QString& settings_file) {
    QJsonDocument document = QJsonDocument::fromJson(catalog);
    QJsonObject root = document.object();
    QJsonObject catalogs = root.value(QStringLiteral("catalogs")).toObject();
    QJsonObject spanish = catalogs.value(QStringLiteral("es")).toObject();
    spanish.remove(QStringLiteral("languageDesc"));
    catalogs.insert(QStringLiteral("es"), spanish);
    root.insert(QStringLiteral("catalogs"), catalogs);

    bool rejected = false;
    try {
        mako::ui::Localization invalid(QJsonDocument(root).toJson(), settings_file, QLocale("en_US"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "incomplete translation catalog was accepted");
}

void test_scaling_catalogs(const QByteArray& catalog, const QString& settings_file) {
    mako::ui::Localization localization(catalog, settings_file, QLocale("en_US"));
    const QStringList language_codes{
        QStringLiteral("en"),
        QStringLiteral("pt-BR"),
        QStringLiteral("pt-PT"),
        QStringLiteral("es"),
        QStringLiteral("ko"),
        QStringLiteral("ja"),
        QStringLiteral("uk"),
        QStringLiteral("zh"),
    };
    const QStringList restart_markers{
        QStringLiteral("(Restart)"),
        QStringLiteral("(Reiniciar)"),
        QStringLiteral("(Reiniciar)"),
        QStringLiteral("(Reiniciar)"),
        QStringLiteral("(재시작)"),
        QStringLiteral("（再起動）"),
        QStringLiteral("(перезапуск)"),
        QStringLiteral("（重启）"),
    };
    const QStringList scaling_keys{
        QStringLiteral("scalingSettings"),
        QStringLiteral("scalingEnabled"),
        QStringLiteral("scalingEnabledDesc"),
        QStringLiteral("scalingEnabledWarning"),
        QStringLiteral("scalingMethod"),
        QStringLiteral("scalingMethodDesc"),
        QStringLiteral("scalingMethodNative"),
        QStringLiteral("scalingMethodMako"),
        QStringLiteral("scalingMethodLs1"),
        QStringLiteral("scalingMethodLs1Performance"),
        QStringLiteral("scalingFactor"),
        QStringLiteral("scalingFactorDesc"),
        QStringLiteral("scalingSupersampling"),
        QStringLiteral("scalingSupersamplingDesc"),
        QStringLiteral("scalingSupersamplingWarning"),
        QStringLiteral("scalingSharpness"),
        QStringLiteral("scalingSharpnessDesc"),
        QStringLiteral("fractionalAdaptive"),
        QStringLiteral("fractionalAdaptiveDesc"),
    };

    const QStringList process_restart_labels{
        QStringLiteral("allowFp16"),
        QStringLiteral("gpu"),
        QStringLiteral("losslessDllPath"),
        QStringLiteral("scalingEnabled"),
        QStringLiteral("ultraPerformance"),
        QStringLiteral("enableZink"),
        QStringLiteral("forceAlsaAudio"),
    };
    const QStringList live_or_recreation_labels{
        QStringLiteral("frameGeneration"),
        QStringLiteral("adaptiveFrameGen"),
        QStringLiteral("fractionalAdaptive"),
        QStringLiteral("multiplier"),
        QStringLiteral("baseFpsCap"),
        QStringLiteral("flowScale"),
        QStringLiteral("performanceMode"),
        QStringLiteral("scalingMethod"),
        QStringLiteral("scalingFactor"),
        QStringLiteral("scalingSupersampling"),
        QStringLiteral("scalingSharpness"),
    };

    for (qsizetype index = 0; index < language_codes.size(); ++index) {
        const QString& language_code = language_codes.at(index);
        const QString& restart_marker = restart_markers.at(index);
        localization.set_language(language_code);
        const QVariantMap strings = localization.strings();
        for (const QString& key : scaling_keys) {
            if (!strings.contains(key) || strings.value(key).toString().isEmpty()) {
                throw std::runtime_error(
                    QStringLiteral("scaling translation %1 is missing from %2")
                        .arg(key, language_code)
                        .toStdString()
                );
            }
        }
        require(strings.value(QStringLiteral("scalingMethodMako")).toString() ==
                QStringLiteral("MAKO Scaler"),
            "MAKO Scaler product name was translated or changed");
        for (const QString& key : process_restart_labels) {
            require(strings.value(key).toString().contains(restart_marker),
                "Process-start control is missing its localized restart marker");
        }
        for (const QString& key : live_or_recreation_labels) {
            require(!strings.value(key).toString().contains(restart_marker),
                "Live or recreation-bound control has a localized restart marker");
        }
    }

    localization.set_language(QStringLiteral("en"));
    const QVariantMap english = localization.strings();
    require(english.value(QStringLiteral("scalingEnabled")).toString() ==
            QStringLiteral("Enable Scaling (Restart)"),
        "English scaling enablement has an unexpected label");
    require(english.value(QStringLiteral("scalingMethod")).toString() ==
            QStringLiteral("Scaling Method"),
        "English scaling method has an unexpected label");
    require(english.value(QStringLiteral("scalingEnabledDesc")).toString() ==
            QStringLiteral("Enable before starting the game. When off, scaling is fully disabled. Supports Lossless Scaling models and MAKO Scaler. Restart the game after changing it."),
        "English scaling help does not match the Decky guidance");
    require(english.value(QStringLiteral("scalingMethodDesc")).toString() ==
            QStringLiteral("Choose the scaling model. You can change it while the game is running."),
        "English scaling-method help does not match the Decky guidance");
    require(english.value(QStringLiteral("scalingFactor")).toString() ==
            QStringLiteral("Scale Factor"),
        "English scale-factor label does not match Decky");
    require(english.value(QStringLiteral("scalingFactorDesc")).toString() ==
            QStringLiteral("Sets the output-to-input size ratio for every method, including Native Resolution. Higher values render fewer source pixels."),
        "English scale-factor help does not match Decky");
    require(english.value(QStringLiteral("scalingSharpnessDesc")).toString() ==
            QStringLiteral("For MAKO, applies this 0–100% multiplier to its 3x sharpening baseline. For LS1, selects one of five learned sharpness variants."),
        "English sharpening help does not describe the MAKO baseline");
    require(english.value(QStringLiteral("flowScaleDesc")).toString() ==
            QStringLiteral("Controls the internal motion-estimation resolution used only for Frame Generation. Lower values reduce GPU work; higher values favour quality."),
        "English Flow Scale help does not match the Frame Generation-only guidance");
    require(english.value(QStringLiteral("fractionalAdaptive")).toString() ==
            QStringLiteral("Fractional Adaptive"),
        "English Fractional Adaptive label does not match Decky");
    require(english.value(QStringLiteral("fractionalAdaptiveDesc")).toString()
            .contains(QStringLiteral("60 real FPS → 90 displayed FPS")),
        "English Fractional Adaptive help does not explain fractional output");
    require(!english.value(QStringLiteral("performanceModeDesc")).toString()
            .contains(QStringLiteral("private frame-generation context")),
        "English lighter-model help exposes an implementation detail");
    require(!english.value(QStringLiteral("maxAdaptiveMultiplierDesc")).toString()
            .contains(QStringLiteral("swaps private resources live")),
        "English Adaptive ceiling help exposes its capacity contract");
    require(!english.value(QStringLiteral("multiplierDesc")).toString()
            .contains(QStringLiteral("swaps private resources live")),
        "English Fixed multiplier help exposes its capacity contract");
    require(english.value(QStringLiteral("ultraPerformanceDesc")).toString()
            .contains(QStringLiteral("compatible controls remain available")),
        "English Ultra Performance help overstates its restart boundary");
}

} // namespace

int main() {
    try {
        const QByteArray catalog = catalog_data();
        QTemporaryDir temporary_directory;
        require(temporary_directory.isValid(), "temporary directory could not be created");

        test_locale_selection(catalog, temporary_directory.path());
        test_persistence(catalog, temporary_directory.path() + "/persistent.ini");
        test_invalid_persisted_language(catalog, temporary_directory.path() + "/invalid.ini");
        test_catalog_validation(catalog, temporary_directory.path() + "/malformed.ini");
        test_scaling_catalogs(catalog, temporary_directory.path() + "/scaling.ini");
    } catch (const std::exception& error) {
        std::cerr << "mako-ui localization test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
