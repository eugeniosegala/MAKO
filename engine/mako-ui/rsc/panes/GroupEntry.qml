import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    property string title
    property string description
    default property alias content: inner.children

    id: root
    Layout.fillWidth: true
    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 120

        Label {
            Layout.fillWidth: true
            text: root.title
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: root.description
            wrapMode: Text.Wrap
            color: Qt.rgba(
                palette.text.r,
                palette.text.g,
                palette.text.b,
                0.7
            )
        }
    }

    ColumnLayout {
        id: inner
        Layout.fillWidth: true
        Layout.minimumWidth: 160
        Layout.preferredWidth: Math.max(160, Math.min(280, root.width * 0.4))
        Layout.maximumWidth: Math.max(160, root.width * 0.5)
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        spacing: 0
    }
}
