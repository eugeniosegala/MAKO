import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

LargeDialog {
    id: root
    objectName: "running_games_dialog"
    property var strings
    signal captured()
    name: strings.detectRunningGame
    onOpened: {
        games.currentIndex = -1;
        backend.refreshRunningGames(show_all.checked);
    }

    Label {
        Layout.fillWidth: true
        text: root.strings.detectRunningGameDesc
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        CheckBox {
            id: show_all
            Layout.fillWidth: true
            text: root.strings.showAllApplications
            enabled: !backend.scanning_games
            onToggled: {
                games.currentIndex = -1;
                backend.refreshRunningGames(checked);
            }
        }
        Button {
            text: root.strings.refreshGames
            enabled: !backend.scanning_games
            onClicked: {
                games.currentIndex = -1;
                backend.refreshRunningGames(show_all.checked);
            }
        }
    }

    ListView {
        id: games
        objectName: "running_game_list"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 80
        clip: true
        model: backend.running_games
        currentIndex: -1
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: games.width
            text: modelData.name + " — PID " + modelData.pid
            highlighted: games.currentIndex === index
            onClicked: games.currentIndex = index
            ToolTip.visible: hovered
            ToolTip.text: modelData.executable
        }
        Label {
            anchors.fill: parent
            visible: backend.scanning_games || games.count === 0
            text: backend.scanning_games ? root.strings.scanningGames : root.strings.noRunningGames
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Label {
        Layout.fillWidth: true
        visible: backend.capture_failed
        text: root.strings.captureGameFailed
        wrapMode: Text.WordWrap
    }

    // Stack actions so translated labels fit at the minimum window size.
    Button {
        Layout.fillWidth: true
        text: root.strings.useGameProfile
        objectName: "use_game_profile"
        enabled: games.currentIndex >= 0 && !backend.scanning_games
        onClicked: {
            if (backend.captureRunningGame(games.currentIndex, true)) {
                root.close();
                root.captured();
            }
        }
    }
    Button {
        Layout.fillWidth: true
        text: root.strings.addGameToProfile
        enabled: backend.available && games.currentIndex >= 0 && !backend.scanning_games
        onClicked: {
            if (backend.captureRunningGame(games.currentIndex, false)) {
                root.close();
                root.captured();
            }
        }
    }
}
