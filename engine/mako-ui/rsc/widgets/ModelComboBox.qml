import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    property int selectionIndex: 0
    property int unavailableFromIndex: -1
    signal choiceActivated(int index)

    currentIndex: selectionIndex
    delegate: ItemDelegate {
        width: control.width
        text: modelData
        enabled: control.unavailableFromIndex < 0 || index < control.unavailableFromIndex
        opacity: enabled ? 1 : 0.45
        highlighted: control.highlightedIndex === index
    }
    onActivated: index => {
        if (unavailableFromIndex < 0 || index < unavailableFromIndex)
            choiceActivated(index)
        // Restore the saved-value binding after keyboard or mouse activation,
        // including attempts to activate an unavailable entry.
        currentIndex = Qt.binding(() => selectionIndex)
    }
}
