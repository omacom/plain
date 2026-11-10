import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Find, and with Ctrl+H replace. Literal text, with a match-case toggle.
Rectangle {
    id: bar

    property var editor
    property bool replacing: false
    property bool caseSensitive: false
    // "Replaced 3", until the search changes.
    property string replaced: ""
    property int matches: 0

    // Counted when the search changes, and a moment after the text stops
    // changing rather than on every keystroke, since it searches the whole file.
    function recount() { matches = visible ? editing.matchCount(findField.text, caseSensitive) : 0 }
    onVisibleChanged: recount()
    onCaseSensitiveChanged: recount()
    Timer { id: recountLater; interval: 150; onTriggered: bar.recount() }
    Connections {
        target: bar.editor.area
        function onTextChanged() { if (bar.visible) recountLater.restart() }
    }

    visible: false
    implicitHeight: column.implicitHeight + 12
    color: theme.darkBackground

    function show(replace) {
        replacing = replace
        visible = true
        const area = editor.area
        if (area.selectionStart !== area.selectionEnd && area.selectedText.indexOf(" ") < 0)
            findField.text = area.selectedText
        findField.forceActiveFocus()
        findField.selectAll()
        recount()
    }

    function hide() {
        visible = false
        editor.focusText()
    }

    function next(backward) {
        if (findField.text.length === 0) {
            show(replacing)
            return
        }
        const area = editor.area
        const from = backward ? area.selectionStart : area.selectionEnd
        const found = editing.find(findField.text, from, backward, caseSensitive)
        if (found.length === 2)
            editor.select(found[0], found[1])
    }

    function selectionMatches() {
        const selected = editor.area.selectedText
        return caseSensitive ? selected === findField.text : selected.toLowerCase() === findField.text.toLowerCase()
    }

    function replaceOne() {
        const area = editor.area
        if (findField.text.length > 0 && selectionMatches())
            area.cursorPosition = editing.replace(area.selectionStart, area.selectionEnd, replaceField.text)
        next(false)
    }

    function replaceAll() {
        const count = editing.replaceAll(findField.text, replaceField.text, caseSensitive)
        replaced = qsTr("Replaced %n", "", count)
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 6
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 6

        RowLayout {
            spacing: 8
            FindField {
                id: findField
                Layout.fillWidth: true
                placeholder: qsTr("Find")
                onTextEdited: {
                    bar.replaced = ""
                    bar.recount()
                    // Find as you type, from where the current match starts.
                    const found = editing.find(text, editor.area.selectionStart, false, bar.caseSensitive)
                    if (found.length === 2)
                        editor.select(found[0], found[1])
                }
                Keys.onReturnPressed: function(event) { bar.next(event.modifiers & Qt.ShiftModifier) }
                Keys.onEnterPressed: function(event) { bar.next(event.modifiers & Qt.ShiftModifier) }
                Keys.onEscapePressed: bar.hide()
            }
            Label {
                visible: text.length > 0
                text: bar.replaced.length > 0 ? bar.replaced
                    : findField.text.length === 0 ? ""
                    : bar.matches === 0 ? qsTr("No matches") : qsTr("%n matches", "", bar.matches)
                color: bar.replaced.length === 0 && bar.matches === 0 ? theme.red : theme.secondaryForeground
            }
            BarButton {
                text: "Aa"
                checkable: true
                checked: bar.caseSensitive
                onClicked: bar.caseSensitive = checked
                tooltip: qsTr("Match case")
            }
            BarButton { text: "↑"; onClicked: bar.next(true); tooltip: qsTr("Previous (Shift+Enter)") }
            BarButton { text: "↓"; onClicked: bar.next(false); tooltip: qsTr("Next (Enter)") }
            BarButton { text: "✕"; onClicked: bar.hide(); tooltip: qsTr("Close (Escape)") }
        }

        RowLayout {
            visible: bar.replacing
            spacing: 8
            FindField {
                id: replaceField
                Layout.fillWidth: true
                placeholder: qsTr("Replace with")
                enabled: doc.editable
                Keys.onReturnPressed: bar.replaceOne()
                Keys.onEnterPressed: bar.replaceOne()
                Keys.onEscapePressed: bar.hide()
            }
            BarButton { text: qsTr("Replace"); enabled: doc.editable && findField.text.length > 0; onClicked: bar.replaceOne() }
            BarButton { text: qsTr("Replace all"); enabled: doc.editable && findField.text.length > 0; onClicked: bar.replaceAll() }
        }
    }
}
