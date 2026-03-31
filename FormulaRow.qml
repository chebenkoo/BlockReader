import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property alias internalName: fNameInput.text
    property alias expression: fExprInput.text
    property var availableFields: []

    spacing: 8
    Layout.fillWidth: true

    TextField {
        id: fNameInput
        placeholderText: "Result Name"
        Layout.preferredWidth: 100
        font.pixelSize: 12
        background: Rectangle {
            color: "#252525"
            border.color: fNameInput.activeFocus ? "#00ff88" : "#444"
            radius: 4
        }
        color: "white"
    }

    Label { text: "="; color: "#888"; font.bold: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: 0

        TextField {
            id: fExprInput
            placeholderText: "Formula (e.g. XSPAN * YSPAN * Density)"
            Layout.fillWidth: true
            font.family: "Monospace"
            font.pixelSize: 12
            background: Rectangle {
                color: "#252525"
                border.color: fExprInput.activeFocus ? "#00ff88" : "#444"
                radius: 4
                // Round only left corners to join with the button
                // (requires Qt 6.4+ or a custom Rect)
                // For simplicity, we'll just keep standard radius.
            }
            color: "white"
        }

        Button {
            id: fieldPickerBtn
            text: "＋ Field"
            flat: false
            font.pixelSize: 11
            Layout.preferredHeight: fExprInput.height
            
            background: Rectangle {
                color: fieldPickerBtn.pressed ? "#1a1a1a" : (fieldPickerBtn.hovered ? "#333" : "#2a2a2a")
                border.color: "#444"
                radius: 4
            }
            palette.buttonText: "#00ff88"

            onClicked: fieldMenu.open()

            Menu {
                id: fieldMenu
                width: 200
                
                // Common spatial fields
                MenuSeparator { contentItem: Rectangle { color: "#444"; implicitHeight: 1; width: parent.width } }
                MenuItem { 
                    text: "XSPAN"; onTriggered: insertText("XSPAN")
                    contentItem: Text { text: parent.text; color: "#aaa"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                }
                MenuItem { 
                    text: "YSPAN"; onTriggered: insertText("YSPAN")
                    contentItem: Text { text: parent.text; color: "#aaa"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                }
                MenuItem { 
                    text: "ZSPAN"; onTriggered: insertText("ZSPAN")
                    contentItem: Text { text: parent.text; color: "#aaa"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                }

                MenuSeparator { contentItem: Rectangle { color: "#444"; implicitHeight: 1; width: parent.width } }

                // Dynamic fields from CSV
                Repeater {
                    model: root.availableFields
                    MenuItem {
                        text: modelData
                        onTriggered: insertText(modelData)
                        contentItem: Text { 
                            text: parent.text
                            color: "white"
                            font.pixelSize: 12
                            verticalAlignment: Text.AlignVCenter 
                        }
                    }
                }

                background: Rectangle {
                    color: "#2a2a2a"
                    border.color: "#444"
                    radius: 4
                }
            }
        }
    }

    function insertText(t) {
        var pos = fExprInput.cursorPosition;
        var full = fExprInput.text;
        fExprInput.text = full.substring(0, pos) + t + full.substring(pos);
        fExprInput.cursorPosition = pos + t.length;
        fExprInput.forceActiveFocus();
    }

    Button {
        text: "×"
        onClicked: root.destroy()
        Layout.preferredWidth: 30
        Layout.preferredHeight: fExprInput.height
        flat: true
        palette.buttonText: "#ff4444"
        font.bold: true
    }
}
