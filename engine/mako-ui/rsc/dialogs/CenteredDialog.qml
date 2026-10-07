import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    property string name
    property bool acceptOnConfirm: true
    default property alias content: inner.children
    signal confirm()

    id: root
    title: name
    width: Math.min(parent.width * 0.8, 520)
    standardButtons: Dialog.Ok
    // A direct DialogButtonBox footer is automatically wired to accept(). Keep
    // confirmation under the caller's control so failed saves stay visible.
    footer: Item {
        implicitWidth: buttons.implicitWidth
        implicitHeight: buttons.implicitHeight
        DialogButtonBox {
            id: buttons
            objectName: "dialog_buttons"
            anchors.fill: parent
            standardButtons: root.standardButtons
            onAccepted: {
                root.confirm();
                if (root.acceptOnConfirm) root.accept();
            }
            onRejected: root.reject()
        }
    }

    modal: true
    dim: true
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2

    contentItem: ColumnLayout {
        id: inner
        spacing: 8
    }
}
