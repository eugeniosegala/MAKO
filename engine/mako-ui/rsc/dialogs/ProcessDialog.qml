import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: t.detectProcess
    signal selected(string processName)

    modal: true
    dim: true
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: parent.width * 0.65
    height: parent.height * 0.7

    onOpened: backend.refreshProcesses()

    contentItem: ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: t.processListDesc
            wrapMode: Text.WordWrap
            color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                Layout.fillWidth: true
                text: t.refresh
                icon.name: "view-refresh"
                onClicked: backend.refreshProcesses()
            }

            Label {
                Layout.fillWidth: true
                text: backend.processCount + " " + t.processesFound
                horizontalAlignment: Text.AlignRight
                color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
            }
        }

        ListView {
            id: processList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.processList
            currentIndex: -1

            delegate: Rectangle {
                width: processList.width
                height: 36
                color: processList.currentIndex === index
                    ? Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.3)
                    : index % 2 === 0 ? palette.alternateBase : "transparent"
                radius: 2

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8

                    Label {
                        Layout.fillWidth: true
                        text: modelData
                        elide: Text.ElideRight
                        color: palette.text
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        processList.currentIndex = index;
                        backend.setSelectedProcessIndex(index);
                    }
                    onDoubleClicked: {
                        processList.currentIndex = index;
                        backend.setSelectedProcessIndex(index);
                        var name = backend.getSelectedProcessName();
                        if (name.length > 0) {
                            root.selected(name);
                            root.close();
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                Layout.fillWidth: true
                text: t.cancel
                onClicked: root.close()
            }

            Button {
                Layout.fillWidth: true
                text: t.selectProcess
                enabled: processList.currentIndex >= 0
                onClicked: {
                    var name = backend.getSelectedProcessName();
                    if (name.length > 0) {
                        root.selected(name);
                        root.close();
                    }
                }
            }
        }
    }
}
