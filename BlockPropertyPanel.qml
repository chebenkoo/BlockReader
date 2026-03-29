import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: 280
    color: Qt.rgba(0.08, 0.08, 0.08, 0.95)
    border.color: "#00ff88"
    border.width: 1
    z: 10

    property var blockData: ({})
    property string filterText: ""
    property bool sortAlphabetical: true
    property bool sortAscending: true

    // Internal logic to filter and sort keys
    readonly property var filteredKeys: {
        var keys = Object.keys(blockData).filter(k => {
            if (k === "_index") return false;
            if (filterText === "") return true;
            return k.toLowerCase().includes(filterText.toLowerCase());
        });

        keys.sort((a, b) => {
            let valA = blockData[a];
            let valB = blockData[b];
            
            let res = 0;
            if (sortAlphabetical) {
                res = a.localeCompare(b);
            } else {
                // Sort by Value (Numeric first, then Alpha)
                let numA = parseFloat(valA);
                let numB = parseFloat(valB);
                if (!isNaN(numA) && !isNaN(numB)) {
                    res = numA - numB;
                } else {
                    res = String(valA).localeCompare(String(valB));
                }
            }
            return sortAscending ? res : -res;
        });

        return keys;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Header
        RowLayout {
            Layout.fillWidth: true
            Text { 
                text: "Block Properties"; 
                color: "#00ff88"; font.bold: true; font.pixelSize: 14; 
                Layout.fillWidth: true 
            }
            Text {
                text: "✕"; color: "#aaa"; font.pixelSize: 18; font.bold: true
                MouseArea { anchors.fill: parent; onClicked: root.visible = false }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

        // Search Bar
        TextField {
            id: searchField
            placeholderText: "Search attributes..."
            Layout.fillWidth: true
            color: "white"
            font.pixelSize: 12
            background: Rectangle { color: "#222"; border.color: searchField.activeFocus ? "#00ff88" : "#444"; radius: 4 }
            onTextChanged: root.filterText = text
            
            Text {
                text: "🔍"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 8
                color: "#666"
                visible: searchField.text === ""
            }
        }

        // Controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Button {
                text: root.sortAlphabetical ? "A-Z" : "Value"
                Layout.fillWidth: true
                onClicked: root.sortAlphabetical = !root.sortAlphabetical
                contentItem: Text {
                    text: parent.text; color: "white"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle { color: "#333"; radius: 2; border.color: "#555" }
            }

            Button {
                text: root.sortAscending ? "↑ Asc" : "↓ Desc"
                Layout.fillWidth: true
                onClicked: root.sortAscending = !root.sortAscending
                contentItem: Text {
                    text: parent.text; color: "white"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle { color: "#333"; radius: 2; border.color: "#555" }
            }
        }

        // List Area
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ListView {
                model: root.filteredKeys
                spacing: 4
                interactive: true
                
                delegate: Rectangle {
                    width: parent.width
                    height: 36
                    color: index % 2 === 0 ? "transparent" : "#1a1a1a"
                    radius: 2

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 1

                        Text {
                            text: modelData
                            color: {
                                let spatial = ["X", "Y", "Z", "X_span", "Y_span", "Z_span"];
                                return spatial.indexOf(modelData) !== -1 ? "#00aaff" : "#aaa";
                            }
                            font.pixelSize: 10
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: {
                                var v = root.blockData[modelData];
                                if (typeof v === "number") return v.toLocaleString(Qt.locale(), 'f', 4);
                                return String(v);
                            }
                            color: "white"
                            font.pixelSize: 11
                            font.family: "Monospace"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // Footer / Summary
        Text {
            text: root.filteredKeys.length + " attributes shown"
            color: "#666"
            font.pixelSize: 9
            Layout.alignment: Qt.AlignRight
        }
    }
}
