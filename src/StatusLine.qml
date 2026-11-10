import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Where you are and what the file is: path, unsaved dot, language, line and
// column, indentation, line endings.
Rectangle {
    id: line

    property var editor
    property bool wrap: false

    implicitHeight: 28
    color: theme.darkBackground

    readonly property var languageNames: ({
        "lua": "Lua", "conf": qsTr("Config"), "toml": "TOML", "jsonc": "JSON", "yaml": "YAML",
        "shell": qsTr("Shell"), "css": "CSS", "text": qsTr("Plain text")
    })

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 18

        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            text: (doc.modified ? "● " : "") + (doc.displayPath.length > 0 ? doc.displayPath : qsTr("Untitled"))
            color: doc.modified ? theme.foreground : theme.secondaryForeground
        }
        Label {
            text: qsTr("Ln %1, Col %2").arg(editing.lineOf(editor.cursorPosition)).arg(editing.columnOf(editor.cursorPosition))
            color: theme.secondaryForeground
        }
        Label {
            text: doc.indentUnit === "\t" ? qsTr("Tabs") : qsTr("Spaces: %1").arg(doc.indentUnit.length)
            color: theme.secondaryForeground
        }
        Label { text: doc.lineEnding; color: theme.secondaryForeground }
        Label { visible: line.wrap; text: qsTr("Wrap"); color: theme.secondaryForeground }
        Label { visible: !doc.editable && doc.path.length > 0; text: qsTr("Read-only"); color: theme.red }
        Label { text: line.languageNames[doc.language] || doc.language; color: theme.secondaryForeground }
    }
}
