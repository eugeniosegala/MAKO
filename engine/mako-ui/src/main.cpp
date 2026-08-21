/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QIcon>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "backend.hpp"
#include "localization.hpp"

using namespace mako::ui;

int main(int argc, char* argv[]) {
    const QGuiApplication app(argc, argv);
    QGuiApplication::setWindowIcon(QIcon(":/rsc/io.github.eugeniosegala.mako.png"));
    QGuiApplication::setOrganizationName("MAKO");
    QGuiApplication::setOrganizationDomain("io.github.eugeniosegala");
    QGuiApplication::setApplicationName("mako-ui");
    QGuiApplication::setApplicationDisplayName("MAKO Renderer");

    QQmlApplicationEngine engine;
    Backend backend;
    Localization localization;

    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("localization", &localization);
    engine.load("qrc:/rsc/UI.qml");

    return QGuiApplication::exec();
}
