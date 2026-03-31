import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    width: 980
    height: 720
    modal: true
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    padding: 0
    topPadding: 0
    bottomPadding: 0

    background: Rectangle {
        color: "#111111"
        border.color: "#00ff8850"
        border.width: 1
        radius: 10
    }

    // ── Data ─────────────────────────────────────────────────────
    property var tableData: []      // current filtered rows
    property var allData:   []      // always unfiltered — for KPI cards
    property int sortColumn: 2      // default: sort by volume desc
    property bool sortAsc: false
    property int selectedRow: -1

    readonly property bool filterActive:
        filterAttrCombo.currentIndex > 0 && filterValCombo.currentText !== ""

    // KPI totals — always from unfiltered allData
    readonly property double totalBlocks: {
        var s = 0; for (var i = 0; i < allData.length; i++) s += allData[i].count; return s;
    }
    readonly property double totalVolume: {
        var s = 0; for (var i = 0; i < allData.length; i++) s += allData[i].volume; return s;
    }
    readonly property double totalTonnes: {
        var s = 0; for (var i = 0; i < allData.length; i++) s += allData[i].tonnes; return s;
    }
    readonly property double totalMetal: {
        var s = 0; for (var i = 0; i < allData.length; i++) s += allData[i].metal; return s;
    }
    readonly property double overallAvgGrade: {
        return totalVolume > 0 ? totalMetal / totalVolume : 0;
    }

    // Filtered subset totals (same as above when no filter)
    readonly property double filtBlocks: {
        var s = 0; for (var i = 0; i < tableData.length; i++) s += tableData[i].count; return s;
    }
    readonly property double filtVolume: {
        var s = 0; for (var i = 0; i < tableData.length; i++) s += tableData[i].volume; return s;
    }
    readonly property double filtTonnes: {
        var s = 0; for (var i = 0; i < tableData.length; i++) s += tableData[i].tonnes; return s;
    }
    readonly property double filtMetal: {
        var s = 0; for (var i = 0; i < tableData.length; i++) s += tableData[i].metal; return s;
    }
    readonly property double filtAvgGrade: {
        return filtVolume > 0 ? filtMetal / filtVolume : 0;
    }

    readonly property double maxVolume: {
        var m = 0;
        for (var i = 0; i < tableData.length; i++) if (tableData[i].volume > m) m = tableData[i].volume;
        return m;
    }
    readonly property double maxGrade: {
        var m = 0;
        for (var i = 0; i < tableData.length; i++) if (tableData[i].avgGrade > m) m = tableData[i].avgGrade;
        return m;
    }

    // Sorted view of tableData
    readonly property var sortedData: {
        var d = tableData.slice();
        var col = sortColumn;
        var asc = sortAsc;
        d.sort(function(a, b) {
            var va, vb;
            if      (col === 0) { va = a.group;    vb = b.group;    return asc ? va.localeCompare(vb) : vb.localeCompare(va); }
            else if (col === 1) { va = a.count;    vb = b.count;    }
            else if (col === 2) { va = a.volume;   vb = b.volume;   }
            else if (col === 3) { va = a.tonnes;   vb = b.tonnes;   }
            else                { va = a.avgGrade; vb = b.avgGrade; }
            return asc ? (va - vb) : (vb - va);
        });
        return d;
    }

    // Stable color per group name (consistent across sort changes)
    function groupColor(groupName) {
        var n = tableData.length;
        if (n <= 0) return "#00ff88";
        for (var k = 0; k < n; k++) {
            if (tableData[k].group === groupName) {
                return Qt.hsva(k / Math.max(n, 1), 0.65, 0.92, 1.0);
            }
        }
        return "#00ff88";
    }

    // ── Content ───────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Title bar ───────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 52
            color: "#1a1a1a"
            radius: 10
            // fill bottom half to remove bottom radius
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 10; color: parent.color }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                spacing: 10

                Rectangle { width: 4; height: 26; radius: 2; color: "#00ff88" }

                Text {
                    text: "Block Model Analytics"
                    color: "white"
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                }

                Text {
                    visible: allData.length > 0
                    text: allData.length + " groups  ·  " + totalBlocks.toLocaleString(Qt.locale(), 'f', 0) + " blocks total"
                    color: "#555"
                    font.pixelSize: 11
                }

                Item { width: 8 }

                // Close
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: closeMa.containsMouse ? "#aa2222" : "#2a2a2a"
                    border.color: "#444"
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 13 }
                    MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; onClicked: root.close() }
                }
            }
        }

        // ── KPI Cards ────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            spacing: 6

            // Row 1: always-visible model totals
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: [
                        { label: "TOTAL BLOCKS",  value: totalBlocks.toLocaleString(Qt.locale(),'f',0),
                          sub: allData.length + " groups",                              accent: "#00ff88", bg1: "#0a2318", bg2: "#0e2b1e" },
                        { label: "TOTAL VOLUME",  value: (totalVolume/1e6).toFixed(3) + " Mm³",
                          sub: totalVolume.toLocaleString(Qt.locale(),'f',0) + " m³",  accent: "#00aaff", bg1: "#0a1a2b", bg2: "#0e2035" },
                        { label: "TOTAL TONNES",  value: (totalTonnes/1e6).toFixed(3) + " Mt",
                          sub: totalTonnes.toLocaleString(Qt.locale(),'f',0) + " t",   accent: "#ffaa00", bg1: "#261a00", bg2: "#2e2000" },
                        { label: "AVG GRADE",     value: overallAvgGrade.toFixed(5),
                          sub: gradeCombo.currentText || "—",                           accent: "#cc44ff", bg1: "#200e2e", bg2: "#281438" }
                    ]
                    delegate: Rectangle {
                        Layout.fillWidth: true; height: 72; radius: 8
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: modelData.bg1 }
                            GradientStop { position: 1.0; color: modelData.bg2 }
                        }
                        border.color: Qt.rgba(Qt.color(modelData.accent).r, Qt.color(modelData.accent).g, Qt.color(modelData.accent).b, 0.25)
                        border.width: 1
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            anchors.topMargin: 8; anchors.bottomMargin: 8; width: 3; radius: 2; color: modelData.accent
                        }
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; anchors.leftMargin: 18; spacing: 2
                            Text { text: modelData.label;  color: modelData.accent; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.8 }
                            Text { text: modelData.value;  color: "white";          font.pixelSize: 19; font.bold: true; font.family: "Monospace" }
                            Text { text: modelData.sub;    color: "#666";           font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                    }
                }
            }

            // Row 2: filtered subset — only shown when filter is active
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                visible: root.filterActive && tableData.length > 0

                // Filter label tag
                Rectangle {
                    height: 42; radius: 6
                    width: 110
                    color: "#1e1a00"; border.color: "#ffcc0040"; border.width: 1
                    ColumnLayout {
                        anchors.centerIn: parent; spacing: 2
                        Text { text: "FILTERED"; color: "#ffcc00"; font.pixelSize: 8; font.bold: true; font.letterSpacing: 2; anchors.horizontalCenter: parent.horizontalCenter }
                        Text {
                            text: filterValCombo.currentText
                            color: "white"; font.pixelSize: 11; font.bold: true
                            elide: Text.ElideRight; width: 90; horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                Repeater {
                    model: [
                        { label: "BLOCKS",  rawV: totalBlocks, filtV: filtBlocks,  fmt: function(v) { return v.toLocaleString(Qt.locale(),'f',0); }, accent: "#00ff88" },
                        { label: "VOLUME",  rawV: totalVolume, filtV: filtVolume,  fmt: function(v) { return (v/1e6).toFixed(3)+" Mm³"; },          accent: "#00aaff" },
                        { label: "TONNES",  rawV: totalTonnes, filtV: filtTonnes,  fmt: function(v) { return (v/1e6).toFixed(3)+" Mt"; },           accent: "#ffaa00" },
                        { label: "GRADE",   rawV: overallAvgGrade, filtV: filtAvgGrade, fmt: function(v) { return v.toFixed(5); },                  accent: "#cc44ff" }
                    ]
                    delegate: Rectangle {
                        Layout.fillWidth: true; height: 42; radius: 6
                        color: "#181818"; border.color: Qt.rgba(Qt.color(modelData.accent).r, Qt.color(modelData.accent).g, Qt.color(modelData.accent).b, 0.2)
                        border.width: 1

                        // Fraction fill bar
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            anchors.margins: 1
                            width: modelData.rawV > 0 ? Math.max(8, (modelData.filtV / modelData.rawV) * parent.width) : 0
                            radius: 5
                            color: Qt.rgba(Qt.color(modelData.accent).r, Qt.color(modelData.accent).g, Qt.color(modelData.accent).b, 0.08)
                        }

                        ColumnLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6; spacing: 1; anchors.topMargin: 5
                            Text { text: modelData.label; color: modelData.accent; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1.5 }
                            RowLayout {
                                spacing: 4
                                Text { text: modelData.fmt(modelData.filtV); color: "white"; font.pixelSize: 12; font.bold: true; font.family: "Monospace" }
                                Text {
                                    text: modelData.rawV > 0 ? "(" + (modelData.filtV/modelData.rawV*100).toFixed(1) + "%)" : ""
                                    color: "#555"; font.pixelSize: 9; font.family: "Monospace"
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Body: sidebar + table ────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            Layout.topMargin: 10
            spacing: 12

            // ── Left sidebar ─────────────────────────────────────
            Rectangle {
                width: 195
                Layout.fillHeight: true
                color: "#181818"
                radius: 8
                border.color: "#2a2a2a"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 0

                    // Section label
                    Text { text: "CONFIGURATION"; color: "#444"; font.pixelSize: 8; font.bold: true; font.letterSpacing: 2.5
                        Layout.bottomMargin: 10 }

                    // Group By
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 4
                        Text { text: "Group By"; color: "#777"; font.pixelSize: 11; Layout.fillWidth: true }
                        Text {
                            text: modelController.stringFields.indexOf(groupCombo.currentText) >= 0 ? "S" : "#"
                            color: modelController.stringFields.indexOf(groupCombo.currentText) >= 0 ? "#ff9900" : "#00aaff"
                            font.pixelSize: 9; font.bold: true
                            visible: groupCombo.currentText !== ""
                        }
                    }
                    ComboBox {
                        id: groupCombo
                        Layout.fillWidth: true
                        Layout.bottomMargin: 12
                        model: modelController.availableFields
                        onCurrentTextChanged: Qt.callLater(refresh)
                        contentItem: Text {
                            leftPadding: 8
                            text: groupCombo.displayText; color: "#00ff88"
                            font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        background: Rectangle { radius: 5; color: "#222"; border.color: "#383838"; border.width: 1 }
                        popup.background: Rectangle { color: "#222"; border.color: "#444"; radius: 5 }
                    }

                    // Grade Field
                    Text { text: "Grade Field"; color: "#777"; font.pixelSize: 11; Layout.bottomMargin: 4 }
                    ComboBox {
                        id: gradeCombo
                        Layout.fillWidth: true
                        Layout.bottomMargin: 12
                        model: modelController.numericFields
                        onCurrentTextChanged: Qt.callLater(refresh)
                        contentItem: Text {
                            leftPadding: 8
                            text: gradeCombo.displayText; color: "#cc44ff"
                            font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        background: Rectangle { radius: 5; color: "#222"; border.color: "#383838"; border.width: 1 }
                        popup.background: Rectangle { color: "#222"; border.color: "#444"; radius: 5 }
                    }

                    // Density Field
                    Text { text: "Density Field"; color: "#777"; font.pixelSize: 11; Layout.bottomMargin: 4 }
                    ComboBox {
                        id: densityCombo
                        Layout.fillWidth: true
                        Layout.bottomMargin: 16
                        model: ["(Constant 1.0)"].concat(modelController.numericFields)
                        onCurrentTextChanged: Qt.callLater(refresh)
                        contentItem: Text {
                            leftPadding: 8
                            text: densityCombo.displayText; color: "#ffaa00"
                            font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        background: Rectangle { radius: 5; color: "#222"; border.color: "#383838"; border.width: 1 }
                        popup.background: Rectangle { color: "#222"; border.color: "#444"; radius: 5 }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#282828"; Layout.bottomMargin: 14 }

                    // Filter section
                    Text { text: "FILTER"; color: "#444"; font.pixelSize: 8; font.bold: true; font.letterSpacing: 2.5
                        Layout.bottomMargin: 10 }

                    Text { text: "Field"; color: "#777"; font.pixelSize: 11; Layout.bottomMargin: 4 }
                    ComboBox {
                        id: filterAttrCombo
                        Layout.fillWidth: true
                        Layout.bottomMargin: 12
                        model: ["(None)"].concat(modelController.stringFields)
                        onCurrentTextChanged: {
                            filterValCombo.model = (currentText === "(None)") ? [] : modelController.getStringValues(currentText);
                            Qt.callLater(refresh);
                        }
                        contentItem: Text {
                            leftPadding: 8
                            text: filterAttrCombo.displayText; color: "white"
                            font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        background: Rectangle { radius: 5; color: "#222"; border.color: "#383838"; border.width: 1 }
                        popup.background: Rectangle { color: "#222"; border.color: "#444"; radius: 5 }
                    }

                    Text {
                        text: "Value"
                        color: filterAttrCombo.currentText !== "(None)" ? "#777" : "#3a3a3a"
                        font.pixelSize: 11
                        Layout.bottomMargin: 4
                    }
                    ComboBox {
                        id: filterValCombo
                        Layout.fillWidth: true
                        Layout.bottomMargin: 16
                        model: []
                        enabled: filterAttrCombo.currentText !== "(None)"
                        onCurrentTextChanged: Qt.callLater(refresh)
                        contentItem: Text {
                            leftPadding: 8
                            text: filterValCombo.displayText
                            color: filterValCombo.enabled ? "white" : "#3a3a3a"
                            font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        background: Rectangle {
                            radius: 5
                            color: filterValCombo.enabled ? "#222" : "#161616"
                            border.color: filterValCombo.enabled ? "#383838" : "#222"
                            border.width: 1
                        }
                        popup.background: Rectangle { color: "#222"; border.color: "#444"; radius: 5 }
                    }

                    Item { Layout.fillHeight: true }

                    // Refresh button
                    Rectangle {
                        Layout.fillWidth: true; height: 38; radius: 7
                        color: refreshMa.pressed ? "#0d2a1e" : (refreshMa.containsMouse ? "#163322" : "#0f2820")
                        border.color: "#00ff8850"; border.width: 1
                        Behavior on color { ColorAnimation { duration: 100 } }

                        RowLayout {
                            anchors.centerIn: parent; spacing: 6
                            Text { text: "↻"; color: "#00ff88"; font.pixelSize: 14; font.bold: true }
                            Text { text: "Refresh"; color: "#00ff88"; font.pixelSize: 12; font.bold: true }
                        }
                        MouseArea { id: refreshMa; anchors.fill: parent; hoverEnabled: true; onClicked: refresh() }
                    }
                }
            }

            // ── Right: Table ──────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                // Column headers (sortable)
                Rectangle {
                    Layout.fillWidth: true; height: 34; color: "#1e1e1e"; radius: 6
                    border.color: "#2a2a2a"; border.width: 1

                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 0

                        Repeater {
                            // col: -1 means not sortable
                            model: [
                                { label: "Material / Group", col: 0, fill: true,  w: 0   },
                                { label: "Blocks",           col: 1, fill: false, w: 90  },
                                { label: "Volume (m³)",      col: 2, fill: false, w: 120 },
                                { label: "Tonnes (t)",       col: 3, fill: false, w: 110 },
                                { label: "Avg Grade",        col: 4, fill: false, w: 106 },
                                { label: "% Vol",            col:-1, fill: false, w: 58  }
                            ]
                            delegate: Item {
                                Layout.fillWidth: modelData.fill
                                Layout.preferredWidth: modelData.fill ? -1 : modelData.w
                                height: 34

                                readonly property bool active: root.sortColumn === modelData.col && modelData.col >= 0

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: modelData.fill ? 0 : 4
                                    spacing: 2

                                    Text {
                                        text: modelData.label
                                        color: active ? "#00ff88" : "#666"
                                        font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.5
                                        Layout.fillWidth: modelData.fill
                                        horizontalAlignment: modelData.fill ? Text.AlignLeft : Text.AlignRight
                                    }
                                    Text {
                                        visible: active
                                        text: root.sortAsc ? " ↑" : " ↓"
                                        color: "#00ff88"; font.pixelSize: 9; font.bold: true
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: modelData.col >= 0
                                    hoverEnabled: true
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: {
                                        if (root.sortColumn === modelData.col) {
                                            root.sortAsc = !root.sortAsc;
                                        } else {
                                            root.sortColumn = modelData.col;
                                            root.sortAsc = (modelData.col === 0);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Table rows
                ListView {
                    id: tableView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.sortedData
                    spacing: 3

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle { radius: 3; color: "#444"; implicitWidth: 6 }
                        background: Rectangle { color: "transparent" }
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: tableView.count === 0
                        text: groupCombo.currentText ? "No data — click Refresh" : "Select a Group By field\nto compute statistics"
                        color: "#3a3a3a"; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter
                        lineHeight: 1.6
                    }

                    delegate: Rectangle {
                        id: row
                        width: tableView.width
                        height: 44
                        radius: 5
                        color: root.selectedRow === index
                               ? Qt.rgba(catCol.r, catCol.g, catCol.b, 0.15)
                               : (index % 2 === 0 ? "#161616" : "#191919")
                        border.color: root.selectedRow === index ? Qt.rgba(catCol.r, catCol.g, catCol.b, 0.5) : "transparent"
                        border.width: 1

                        readonly property color catCol: root.groupColor(modelData.group)

                        // Volume bar (background watermark)
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: root.maxVolume > 0 ? (modelData.volume / root.maxVolume) * parent.width : 0
                            color: Qt.rgba(row.catCol.r, row.catCol.g, row.catCol.b, 0.05)
                            radius: 5
                        }

                        // Left color strip
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            anchors.topMargin: 6; anchors.bottomMargin: 6
                            width: 3; radius: 1.5; color: row.catCol
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 0

                            // Group name
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    width: 9; height: 9; radius: 4.5; color: row.catCol
                                }
                                Text {
                                    text: modelData.group
                                    color: row.catCol
                                    font.pixelSize: 12; font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            // Blocks
                            Text {
                                text: modelData.count.toLocaleString(Qt.locale(), 'f', 0)
                                color: "white"; font.pixelSize: 12; font.family: "Monospace"
                                Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight
                            }

                            // Volume
                            Text {
                                text: modelData.volume.toLocaleString(Qt.locale(), 'f', 0)
                                color: "#00aaff"; font.pixelSize: 12; font.family: "Monospace"
                                Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight
                            }

                            // Tonnes
                            Text {
                                text: modelData.tonnes.toLocaleString(Qt.locale(), 'f', 0)
                                color: "#ffaa00"; font.pixelSize: 12; font.family: "Monospace"
                                Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight
                            }

                            // Grade + mini bar
                            Item {
                                Layout.preferredWidth: 106
                                height: parent.height

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.topMargin: 8; anchors.bottomMargin: 8
                                    anchors.leftMargin: 4; anchors.rightMargin: 4
                                    spacing: 4

                                    Text {
                                        text: modelData.avgGrade.toFixed(4)
                                        color: "#cc44ff"; font.pixelSize: 12; font.family: "Monospace"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; height: 3; radius: 1.5; color: "#252525"
                                        Rectangle {
                                            width: root.maxGrade > 0 ? Math.max(3, (modelData.avgGrade / root.maxGrade) * parent.width) : 0
                                            height: parent.height; radius: 1.5; color: "#cc44ff"
                                        }
                                    }
                                }
                            }

                            // % Volume
                            Text {
                                text: root.totalVolume > 0 ? (modelData.volume / root.totalVolume * 100).toFixed(1) + "%" : "—"
                                color: "#555"; font.pixelSize: 11; font.family: "Monospace"
                                Layout.preferredWidth: 58; horizontalAlignment: Text.AlignRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectedRow = (root.selectedRow === index) ? -1 : index
                        }
                    }
                }

                // ── Totals footer ─────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 6
                    visible: root.tableData.length > 0
                    color: "#141f18"
                    border.color: "#00ff8828"; border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14; anchors.rightMargin: 10
                        spacing: 0

                        Text { text: "TOTAL  (" + root.tableData.length + ")"; color: "#00ff88"; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1; Layout.fillWidth: true }
                        Text { text: root.totalBlocks.toLocaleString(Qt.locale(),'f',0); color: "white"; font.pixelSize: 12; font.family: "Monospace"; font.bold: true; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                        Text { text: root.totalVolume.toLocaleString(Qt.locale(),'f',0); color: "#00aaff"; font.pixelSize: 12; font.family: "Monospace"; font.bold: true; Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight }
                        Text { text: root.totalTonnes.toLocaleString(Qt.locale(),'f',0); color: "#ffaa00"; font.pixelSize: 12; font.family: "Monospace"; font.bold: true; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                        Text { text: root.overallAvgGrade.toFixed(4); color: "#cc44ff"; font.pixelSize: 12; font.family: "Monospace"; font.bold: true; Layout.preferredWidth: 106; horizontalAlignment: Text.AlignRight }
                        Text { text: "100%"; color: "#444"; font.pixelSize: 11; font.family: "Monospace"; Layout.preferredWidth: 58; horizontalAlignment: Text.AlignRight }
                    }
                }
            }
        }
    }

    // ── Logic ─────────────────────────────────────────────────────
    function refresh() {
        if (!groupCombo.currentText || !gradeCombo.currentText) return;
        var density = (densityCombo.currentText === "(Constant 1.0)") ? "" : densityCombo.currentText;
        var fField  = (filterAttrCombo.currentText === "(None)")      ? "" : filterAttrCombo.currentText;
        var fValue  = filterValCombo.currentText;

        // Always fetch unfiltered totals for KPI cards
        root.allData     = modelController.getSummary(groupCombo.currentText, gradeCombo.currentText, density, "", "");
        // Fetch filtered (same as allData when no filter)
        root.tableData   = (fField !== "") ? modelController.getSummary(groupCombo.currentText, gradeCombo.currentText, density, fField, fValue)
                                           : root.allData;
        root.selectedRow = -1;
    }

    onOpened: refresh()
}
