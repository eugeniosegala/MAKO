/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        QQmlApplicationEngine engine;
        const auto directory = QUrl::fromLocalFile(QString::fromUtf8(MAKO_UI_DIALOGS_DIRECTORY)).toString();
        engine.loadData((QStringLiteral(R"QML(
import QtQuick
import QtQuick.Controls
import ")QML") + directory + QStringLiteral(R"QML("
ApplicationWindow {
    width: 700
    height: 400
    visible: true
    CenteredDialog {
        id: profile
        objectName: "profile_dialog"
        name: "Profile"
        acceptOnConfirm: false
        property bool saved: false
        property int attempts: 0
        onConfirm: {
            attempts++;
            if (saved) close();
        }
        Label { text: "A failed save must remain visible." }
    }
    Component.onCompleted: profile.open()
}
)QML")).toUtf8());
        require(!engine.rootObjects().empty(), "unable to load profile dialog fixture");
        application.processEvents();
        auto* dialog = engine.rootObjects().front()->findChild<QObject*>(QStringLiteral("profile_dialog"));
        require(dialog && dialog->property("visible").toBool(), "profile dialog did not open");
        require(dialog->property("width").toDouble() <= 560, "profile dialog exceeds the minimum window width");
        auto* buttons = dialog->findChild<QObject*>(QStringLiteral("dialog_buttons"));
        require(buttons, "profile confirmation buttons are missing");
        require(QMetaObject::invokeMethod(buttons, "accepted"), "unable to confirm failed save");
        application.processEvents();
        require(dialog->property("visible").toBool() && dialog->property("attempts").toInt() == 1,
            "Qt automatically closed the dialog after a failed save");
        dialog->setProperty("saved", true);
        require(QMetaObject::invokeMethod(buttons, "accepted"), "unable to retry confirmation");
        application.processEvents();
        require(!dialog->property("visible").toBool() && dialog->property("attempts").toInt() == 2,
            "successful retry did not close the dialog exactly once");
        dialog->setProperty("acceptOnConfirm", true);
        dialog->setProperty("saved", false);
        require(QMetaObject::invokeMethod(dialog, "open") && QMetaObject::invokeMethod(buttons, "accepted"),
            "unable to confirm an ordinary dialog");
        application.processEvents();
        require(!dialog->property("visible").toBool(), "ordinary dialogs stopped accepting OK");
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: profile dialog test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
