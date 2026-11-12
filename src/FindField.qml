import QtQuick

// A compact text field at the desktop's text size, for the find bar. Not a
// Material TextField, whose floating label and 56px height dwarf a 12px
// editor; the placeholder sits inside and disappears as you type.
TextInput {
    id: field

    property string placeholder
    // Dim text at the right end, like "2 matches", that typing stops short of.
    property string hint
    property color hintColor: theme.secondaryForeground

    leftPadding: 8
    rightPadding: hint.length > 0 ? hintLabel.implicitWidth + 16 : 8
    topPadding: 5
    bottomPadding: 5
    verticalAlignment: TextInput.AlignVCenter
    clip: true
    selectByMouse: true
    font.pixelSize: theme.textSize
    color: theme.foreground
    selectionColor: win.tint(theme.accent, 0.35)
    selectedTextColor: theme.brightForeground
    opacity: enabled ? 1 : 0.5

    Rectangle {
        z: -1
        anchors.fill: parent
        color: theme.background
        radius: Math.min(4, theme.radius)
        border.width: 1
        border.color: field.activeFocus ? theme.accent : win.tint(theme.foreground, 0.2)
    }

    Text {
        anchors.fill: parent
        leftPadding: field.leftPadding
        verticalAlignment: Text.AlignVCenter
        text: field.placeholder
        font: field.font
        color: theme.secondaryForeground
        visible: field.text.length === 0 && field.preeditText.length === 0
    }

    Text {
        id: hintLabel
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: field.hint
        font: field.font
        color: field.hintColor
    }
}
