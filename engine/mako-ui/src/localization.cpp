/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "localization.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QSet>
#include <QtGlobal>

#include <stdexcept>

namespace mako::ui {
namespace {

constexpr auto SETTINGS_KEY = "interface/language";
constexpr auto DEFAULT_LANGUAGE = "en";
constexpr auto CATALOG_RESOURCE = ":/rsc/i18n/translations.json";

[[noreturn]] void invalid_catalog(const QString& reason) {
    throw std::runtime_error(
        QStringLiteral("Invalid MAKO UI translation catalog: %1").arg(reason).toStdString());
}

QByteArray load_catalog_resource() {
    QFile file(QString::fromLatin1(CATALOG_RESOURCE));
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Failed to open the MAKO UI translation catalog");
    }
    return file.readAll();
}

} // namespace

Localization::Localization(QObject* parent) : QObject(parent) {
    initialize(load_catalog_resource(), {}, QLocale::system());
}

Localization::Localization(const QByteArray& catalog_data, const QString& settings_file,
                           const QLocale& system_locale, QObject* parent)
    : QObject(parent) {
    initialize(catalog_data, settings_file, system_locale);
}

Localization::~Localization() = default;

QString Localization::language() const {
    return language_;
}

void Localization::set_language(const QString& language) {
    if (!language_codes_.contains(language) || language_ == language) {
        return;
    }

    language_ = language;
    settings_->setValue(QString::fromLatin1(SETTINGS_KEY), language_);
    settings_->sync();
    emit language_changed();
}

int Localization::language_index() const {
    return language_codes_.indexOf(language_);
}

void Localization::set_language_index(const int index) {
    if (index < 0 || index >= language_codes_.size()) {
        return;
    }
    set_language(language_codes_.at(index));
}

QStringList Localization::language_names() const {
    return language_names_;
}

QVariantMap Localization::strings() const {
    return catalogs_.value(language_).toMap();
}

QString Localization::language_for_locale(const QLocale& locale) {
    if (locale.language() == QLocale::Portuguese) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        const bool is_brazil = locale.territory() == QLocale::Brazil;
#else
        const bool is_brazil = locale.country() == QLocale::Brazil;
#endif
        return is_brazil ? QStringLiteral("pt-BR") : QStringLiteral("pt-PT");
    }
    if (locale.language() == QLocale::Spanish) {
        return QStringLiteral("es");
    }
    if (locale.language() == QLocale::Korean) {
        return QStringLiteral("ko");
    }
    if (locale.language() == QLocale::Japanese) {
        return QStringLiteral("ja");
    }
    if (locale.language() == QLocale::Chinese) {
        return QStringLiteral("zh");
    }
    return QString::fromLatin1(DEFAULT_LANGUAGE);
}

void Localization::initialize(const QByteArray& catalog_data, const QString& settings_file,
                              const QLocale& system_locale) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(catalog_data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        invalid_catalog(parse_error.errorString());
    }

    const QJsonObject root = document.object();
    const QJsonArray languages = root.value(QStringLiteral("languages")).toArray();
    const QJsonObject catalogs = root.value(QStringLiteral("catalogs")).toObject();
    if (languages.isEmpty() || catalogs.isEmpty()) {
        invalid_catalog(QStringLiteral("languages and catalogs must be non-empty"));
    }

    QSet<QString> unique_codes;
    for (const QJsonValue& value : languages) {
        const QJsonObject language = value.toObject();
        const QString code = language.value(QStringLiteral("code")).toString();
        const QString name = language.value(QStringLiteral("name")).toString();
        if (code.isEmpty() || name.isEmpty() || unique_codes.contains(code)) {
            invalid_catalog(QStringLiteral("each language needs a unique code and display name"));
        }
        unique_codes.insert(code);
        language_codes_.append(code);
        language_names_.append(name);
    }

    if (!unique_codes.contains(QString::fromLatin1(DEFAULT_LANGUAGE))) {
        invalid_catalog(QStringLiteral("the English fallback catalog is missing"));
    }

    const QJsonObject english_catalog =
        catalogs.value(QString::fromLatin1(DEFAULT_LANGUAGE)).toObject();
    if (english_catalog.isEmpty()) {
        invalid_catalog(QStringLiteral("the English fallback catalog is missing or empty"));
    }
    QStringList expected_keys = english_catalog.keys();
    expected_keys.sort();

    for (const QString& code : language_codes_) {
        const QJsonObject catalog = catalogs.value(code).toObject();
        if (catalog.isEmpty()) {
            invalid_catalog(QStringLiteral("catalog %1 is missing or empty").arg(code));
        }

        QStringList keys = catalog.keys();
        keys.sort();
        if (keys != expected_keys) {
            invalid_catalog(QStringLiteral("catalog %1 does not match the English keys").arg(code));
        }

        for (const QString& key : keys) {
            if (!catalog.value(key).isString() || catalog.value(key).toString().isEmpty()) {
                invalid_catalog(
                    QStringLiteral("catalog %1 contains an empty value for %2").arg(code, key));
            }
        }
        catalogs_.insert(code, catalog.toVariantMap());
    }

    for (const QString& code : catalogs.keys()) {
        if (!unique_codes.contains(code)) {
            invalid_catalog(QStringLiteral("catalog %1 has no language declaration").arg(code));
        }
    }

    if (settings_file.isEmpty()) {
        settings_ = std::make_unique<QSettings>();
    } else {
        settings_ = std::make_unique<QSettings>(settings_file, QSettings::IniFormat);
    }

    language_ = settings_->value(QString::fromLatin1(SETTINGS_KEY)).toString();
    if (!language_codes_.contains(language_)) {
        language_ = language_for_locale(system_locale);
        if (!language_codes_.contains(language_)) {
            language_ = QString::fromLatin1(DEFAULT_LANGUAGE);
        }
        settings_->setValue(QString::fromLatin1(SETTINGS_KEY), language_);
        settings_->sync();
    }
}

} // namespace mako::ui
