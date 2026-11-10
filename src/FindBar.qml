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
    readonly property int matches: { doc.modified; findField.text; bar.caseSensitive; return visible ? editing.matchCount(findField.text, caseSensitive) : 0 }

    visible: false
    implicitHeight: column.implicitHeight + 16
    color: theme.darkBackground

    function show(replace) {
        replacing = replace
        visible = true
        const area = editor.area
        if (area.selectionStart !== area.selectionEnd && area.selectedText.indexOf(" ") < 0)
            findField.text = area.selectedText
        findField.forceActiveFocus()
        findField.selectAll()
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
        anchors.margins: 8
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 6

        RowLayout {
            spacing: 8
            TextField {
                id: findField
                Layout.fillWidth: true
                placeholderText: qsTr("Find")
                selectByMouse: true
                onTextEdited: {
                    bar.replaced = ""
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
            Button {
                text: "Aa"
                flat: true
                checkable: true
                checked: bar.caseSensitive
                onToggled: bar.caseSensitive = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Match case")
            }
            ToolButton { text: "↑"; onClicked: bar.next(true); ToolTip.visible: hovered; ToolTip.text: qsTr("Previous (Shift+Enter)") }
            ToolButton { text: "↓"; onClicked: bar.next(false); ToolTip.visible: hovered; ToolTip.text: qsTr("Next (Enter)") }
            ToolButton { text: "✕"; onClicked: bar.hide(); ToolTip.visible: hovered; ToolTip.text: qsTr("Close (Escape)") }
        }

        RowLayout {
            visible: bar.replacing
            spacing: 8
            TextField {
                id: replaceField
                Layout.fillWidth: true
                placeholderText: qsTr("Replace with")
                selectByMouse: true
                enabled: doc.editable
                Keys.onReturnPressed: bar.replaceOne()
                Keys.onEnterPressed: bar.replaceOne()
                Keys.onEscapePressed: bar.hide()
            }
            Button { text: qsTr("Replace"); flat: true; enabled: doc.editable && findField.text.length > 0; onClicked: bar.replaceOne() }
            Button { text: qsTr("Replace all"); flat: true; enabled: doc.editable && findField.text.length > 0; onClicked: bar.replaceAll() }
        }
    }
}
