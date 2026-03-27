import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property alias internalName: nameInput.text
    property alias csvField: fieldCombo.currentText
    property var availableFields: []

    spacing: 5
    Layout.fillWidth: true

    TextField {
        id: nameInput
        placeholderText: "Attribute Name"
        Layout.preferredWidth: 120
        background: Rectangle {
            color: "#333"
            border.color: nameInput.activeFocus ? "#00ff00" : "#555"
        }
        color: "white"
    }

    ComboBox {
        id: fieldCombo
        model: {
            var m = ["(None)"];
            if (availableFields && availableFields.length > 0) {
                for (var i = 0; i < availableFields.length; i++) {
                    m.push(availableFields[i]);
                }
            }
            return m;
        }
        Layout.fillWidth: true
    }

    Button {
        text: "×"
        onClicked: root.destroy()
        Layout.preferredWidth: 30
        palette.buttonText: "red"
    }
}
