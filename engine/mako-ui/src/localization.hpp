/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <QByteArray>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class QSettings;

namespace mako::ui {

class Localization final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE set_language NOTIFY language_changed)
    Q_PROPERTY(int language_index READ language_index WRITE set_language_index NOTIFY language_changed)
    Q_PROPERTY(QStringList language_names READ language_names CONSTANT)
    Q_PROPERTY(QVariantMap strings READ strings NOTIFY language_changed)

  public:
    explicit Localization(QObject* parent = nullptr);
    Localization(const QByteArray& catalog_data, const QString& settings_file,
                 const QLocale& system_locale, QObject* parent = nullptr);
    ~Localization() override;

    [[nodiscard]] QString language() const;
    void set_language(const QString& language);

    [[nodiscard]] int language_index() const;
    void set_language_index(int index);

    [[nodiscard]] QStringList language_names() const;
    [[nodiscard]] QVariantMap strings() const;

    [[nodiscard]] static QString language_for_locale(const QLocale& locale);

  signals:
    void language_changed();

  private:
    void initialize(const QByteArray& catalog_data, const QString& settings_file,
                    const QLocale& system_locale);

    QStringList language_codes_;
    QStringList language_names_;
    QVariantMap catalogs_;
    QString language_;
    std::unique_ptr<QSettings> settings_;
};

} // namespace mako::ui
