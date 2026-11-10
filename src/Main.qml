import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 980
    height: 720
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: (doc.modified ? "● " : "") + doc.fileName + " — Plain"
    color: theme.background

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.foreground: theme.foreground
    Material.background: theme.background
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // What to do once the unsaved-changes question is answered: "close" or "open".
    property string pendingAction: ""
    // Set once the user agreed to lose changes, so closing doesn't ask again.
    property bool discardConfirmed: false

    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    Settings {
        id: settings
        location: StandardPaths.writableLocation(StandardPaths.ConfigLocation) + "/plain/settings.conf"
        property int fontSize: 14
        property bool wrap: false
    }

    function save() {
        Qt.inputMethod.commit()
        if (doc.path.length === 0) {
            picker.saveFile("")
            return false
        }
        return doc.save()
    }

    function saveAs() {
        Qt.inputMethod.commit()
        picker.saveFile(doc.path)
    }

    // Close or open-another, asking first about unsaved changes.
    function request(action) {
        if (doc.modified && !discardConfirmed) {
            pendingAction = action
            unsaved.open()
            return
        }
        finish(action)
    }

    function finish(action) {
        pendingAction = ""
        if (action === "close") {
            discardConfirmed = true
            win.close()
        } else if (action === "open") {
            picker.openFile(doc.path.length > 0 ? doc.path.substring(0, doc.path.lastIndexOf("/")) : "")
        }
    }

    onClosing: function(close) {
        if (doc.modified && !discardConfirmed) {
            close.accepted = false
            request("close")
        }
    }

    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: win.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; context: Qt.ApplicationShortcut; onActivated: win.saveAs() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: win.request("open") }
    Shortcut { sequences: ["Ctrl+W", "Ctrl+Q"]; context: Qt.ApplicationShortcut; onActivated: win.request("close") }
    Shortcut { sequence: "Ctrl+F"; context: Qt.ApplicationShortcut; onActivated: findBar.show(false) }
    Shortcut { sequence: "Ctrl+H"; context: Qt.ApplicationShortcut; onActivated: findBar.show(true) }
    Shortcut { sequences: ["F3", StandardKey.FindNext]; context: Qt.ApplicationShortcut; onActivated: findBar.next(false) }
    Shortcut { sequences: ["Shift+F3", StandardKey.FindPrevious]; context: Qt.ApplicationShortcut; onActivated: findBar.next(true) }
    Shortcut { sequence: "Ctrl+G"; context: Qt.ApplicationShortcut; onActivated: goToLine.open() }
    Shortcut { sequence: "Ctrl+/"; context: Qt.ApplicationShortcut; enabled: doc.editable; onActivated: editor.toggleComment() }
    Shortcut { sequence: "Alt+Z"; context: Qt.ApplicationShortcut; onActivated: settings.wrap = !settings.wrap }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++", StandardKey.ZoomIn]; context: Qt.ApplicationShortcut; onActivated: settings.fontSize = Math.min(48, settings.fontSize + 1) }
    Shortcut { sequences: ["Ctrl+-", StandardKey.ZoomOut]; context: Qt.ApplicationShortcut; onActivated: settings.fontSize = Math.max(8, settings.fontSize - 1) }
    Shortcut { sequence: "Ctrl+0"; context: Qt.ApplicationShortcut; onActivated: settings.fontSize = 14 }
    Shortcut { sequences: ["F1", "Ctrl+?"]; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }

    Connections {
        target: picker
        function onOpenSelected(url) {
            win.discardConfirmed = false
            doc.openUrl(url)
            editor.goToLine(1)
        }
        function onSaveSelected(url) {
            if (doc.saveAsUrl(url) && win.pendingAction === "close")
                win.finish("close")
        }
        function onFailed(message) { pickerError.text = message }
    }

    Component.onCompleted: {
        doc.attach(editor.textDocument)
        if (startPath.length > 0)
            doc.open(startPath)
        editor.goToLine(Math.max(1, startLine))
    }

    // Right-to-left languages mirror the chrome; the text keeps its own
    // direction line by line.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Banner {
                Layout.fillWidth: true
                onSaveAsRequested: win.saveAs()
                onDone: editor.focusText()
            }

            Text {
                id: pickerError
                Layout.fillWidth: true
                Layout.margins: 10
                visible: text.length > 0
                color: theme.red
                wrapMode: Text.Wrap
                MouseArea { anchors.fill: parent; onClicked: pickerError.text = "" }
            }

            EditorView {
                id: editor
                objectName: "editorView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                fontSize: settings.fontSize
                wrap: settings.wrap
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
            }

            FindBar {
                id: findBar
                Layout.fillWidth: true
                editor: editor
            }

            StatusLine {
                Layout.fillWidth: true
                editor: editor
                wrap: settings.wrap
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }
    }

    GoToLine {
        id: goToLine
        anchors.centerIn: parent
        lines: editing.lineCount()
        onAccepted: function(line) { editor.goToLine(line) }
        onClosed: editor.focusText()
    }

    // Unsaved changes: Save, Don't save, Cancel.
    Dialog {
        id: unsaved
        anchors.centerIn: parent
        modal: true
        title: qsTr("Save changes to %1?").arg(doc.fileName)
        Label {
            text: qsTr("Your changes will be lost if you don't save them.")
            color: theme.foreground
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Save")
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Don't save")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            onClicked: function(button) {
                const role = button.DialogButtonBox.buttonRole
                unsaved.close()
                if (role === DialogButtonBox.AcceptRole) {
                    // An untitled file finishes in onSaveSelected; a failed
                    // save leaves the banner up and the window open.
                    if (win.save())
                        win.finish(win.pendingAction)
                } else if (role === DialogButtonBox.DestructiveRole) {
                    win.discardConfirmed = win.pendingAction === "close"
                    win.finish(win.pendingAction)
                } else {
                    win.pendingAction = ""
                    editor.focusText()
                }
            }
        }
    }
}
