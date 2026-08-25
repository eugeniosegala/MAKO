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
    } catch (const std::exception& error) {
        std::cerr << "mako-ui localization test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
