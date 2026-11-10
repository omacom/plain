import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Ctrl+G: a line number, Enter, there.
Popup {
    id: popup

    property int lines: 1
    signal accepted(int line)

    modal: true
    focus: true
    padding: 16
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }
    onOpened: { field.text = ""; field.forceActiveFocus() }

    background: DialogBackground {}

    contentItem: ColumnLayout {
        spacing: 8
        Label { text: qsTr("Go to line"); color: theme.brightForeground }
        TextField {
            id: field
            Layout.preferredWidth: 220
            placeholderText: qsTr("1 to %1").arg(popup.lines)
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1 }
            Keys.onReturnPressed: popup.go()
            Keys.onEnterPressed: popup.go()
        }
    }

    function go() {
        const line = parseInt(field.text)
        if (line > 0) {
            close()
            accepted(line)
        }
    }
}
