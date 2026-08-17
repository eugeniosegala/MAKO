/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QIcon>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "backend.hpp"

using namespace mako::ui;

int main(int argc, char* argv[]) {
    const QGuiApplication app(argc, argv);
    QGuiApplication::setWindowIcon(QIcon(":/rsc/io.github.eugeniosegala.mako.png"));
    QGuiApplication::setApplicationName("mako-ui");
    QGuiApplication::setApplicationDisplayName("MAKO Renderer");

    QQmlApplicationEngine engine;
    Backend backend;

    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load("qrc:/rsc/UI.qml");

    return QGuiApplication::exec();
}
