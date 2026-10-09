import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    property string title
    property string description
    property string experimentalLabel: ""
    property bool compactRestartMarker: false
    default property alias content: inner.children

    function escapeRichText(value) {
        return value
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/\"/g, "&quot;");
    }

    function formattedTitle(value) {
        const match = /^(.*?)(\s*)(\([^()]+\)|（[^（）]+）)$/.exec(value);
        if (!match)
            return escapeRichText(value);

        const separator = match[2].length > 0 ? "&nbsp;" : "";
        return escapeRichText(match[1])
            + separator
            + '<span style="font-size: 72%; font-weight: normal;">'
            + escapeRichText(match[3])
            + "</span>";
    }

    id: root
    Layout.fillWidth: true
    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 120

        Label {
            Layout.fillWidth: true
            text: root.compactRestartMarker
                ? root.formattedTitle(root.title)
                : root.title
            textFormat: root.compactRestartMarker ? Text.RichText : Text.PlainText
            font.bold: true
            wrapMode: Text.Wrap
        }

        Label {
            visible: root.experimentalLabel.length > 0
            text: root.experimentalLabel
            textFormat: Text.PlainText
            font.pixelSize: 10
            font.bold: true
            font.capitalization: Font.AllUppercase
            leftPadding: 6
            rightPadding: 6
            topPadding: 2
            bottomPadding: 2
            background: Rectangle {
                radius: height / 2
                color: Qt.rgba(0.96, 0.64, 0.35, 0.16)
                border.color: Qt.rgba(0.96, 0.64, 0.35, 0.5)
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.description
            wrapMode: Text.Wrap
            font.pixelSize: 10
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
