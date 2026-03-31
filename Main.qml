import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick3D
import QtQuick3D.Helpers
import Mining 1.0

Window {
    width: 1280
    height: 720
    visible: true
    title: "Tactical Optimizer - Phase 1: Digital Twin Engine"
    color: "#212121"

    FileDialog {
        id: fileDialog
        title: "Select Block Model"
        nameFilters: ["Block Model files (*.csv *.dat)", "CSV files (*.csv)", "Micromine files (*.dat)"]
        onAccepted: modelController.preScan(selectedFile)
    }

    // Open the mapping dialog once when preScan finds fields.
    // Do NOT use a reactive visible-binding — it would reopen the dialog
    // when the main-thread lambda fires emit availableFieldsChanged() while
    // status is still "Computing…", triggering 83-row QML creation that
    // blocks the main thread and stalls the renderer.
    Connections {
        target: modelController
        function onAvailableFieldsChanged() {
            // Only open when idle (not loading) and fields are fresh from preScan.
            if (modelController.availableFields.length > 0 && !modelController.isLoading) {
                mappingDialog.open()
            }
        }
    }

    Dialog {
        id: mappingDialog
        title: "Map Block Model Fields"
        width: 500
        height: Math.min(600, parent.height * 0.9)
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton // We use our own "Load Model" button

        property var optionalFields: {
            var f = ["(None)"];
            if (modelController.availableFields) {
                for (var i = 0; i < modelController.availableFields.length; i++) {
                    f.push(modelController.availableFields[i]);
                }
            }
            return f;
        }

        onVisibleChanged: {
            if (visible && modelController.availableFields.length > 0) {
                // Clear existing custom attributes to avoid duplicates
                while(customAttributesList.children.length > 0) {
                    customAttributesList.children[0].destroy();
                }

                // Auto-map everything that isn't a known spatial field
                let fields = modelController.availableFields;
                let spatial = ["EAST", "NORTH", "RL", "_EAST", "_NORTH", "_RL", "X", "Y", "Z", "XSPAN", "YSPAN", "ZSPAN"];
                
                for (let i = 0; i < fields.length; i++) {
                    let field = fields[i];
                    if (spatial.indexOf(field.toUpperCase()) === -1) {
                        console.log("Auto-mapping field: " + field);
                        attributeRowComponent.createObject(customAttributesList, {
                            "internalName": field,
                            "initialCsvField": field,
                            "availableFields": modelController.availableFields
                        });
                    }
                }
            }
        }

        ScrollView {
            anchors.fill: parent
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

            ColumnLayout {
                width: parent.width - 20
                spacing: 15
                anchors.margins: 10

                Label { text: "Map Spatial Coordinates (Mandatory):"; font.bold: true; font.pixelSize: 14 }
                
                GridLayout {
                    columns: 2
                    Layout.fillWidth: true
                    rowSpacing: 10
                    
                    Label { text: "X (East):" }
                    ComboBox { id: comboX; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("EAST")); Layout.fillWidth: true }
                    
                    Label { text: "Y (North):" }
                    ComboBox { id: comboY; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("NORTH")); Layout.fillWidth: true }
                    
                    Label { text: "Z (Elevation):" }
                    ComboBox { id: comboZ; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("RL")); Layout.fillWidth: true }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

                Label { text: "Block Dimensions (Spans):"; font.bold: true; font.pixelSize: 14 }
                
                GridLayout {
                    columns: 2
                    Layout.fillWidth: true
                    rowSpacing: 10

                    Label { text: "X Span:" }
                    ComboBox { id: comboXS; model: mappingDialog.optionalFields; currentIndex: Math.max(0, mappingDialog.optionalFields.indexOf("_EAST")); Layout.fillWidth: true }

                    Label { text: "Y Span:" }
                    ComboBox { id: comboYS; model: mappingDialog.optionalFields; currentIndex: Math.max(0, mappingDialog.optionalFields.indexOf("_NORTH")); Layout.fillWidth: true }

                    Label { text: "Z Span:" }
                    ComboBox { id: comboZS; model: mappingDialog.optionalFields; currentIndex: Math.max(0, mappingDialog.optionalFields.indexOf("_RL")); Layout.fillWidth: true }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

                Label { text: "Custom Attributes (Multi-Material):"; font.bold: true; font.pixelSize: 14 }
                
                ColumnLayout {
                    id: customAttributesList
                    Layout.fillWidth: true
                    spacing: 8
                }

                Component {
                    id: attributeRowComponent
                    RowLayout {
                        id: rowRoot
                        property alias internalName: nameInput.text
                        property string initialCsvField: ""
                        property var availableFields: []
                        readonly property string csvField: fieldCombo.currentText

                        spacing: 5
                        Layout.fillWidth: true

                        TextField {
                            id: nameInput
                            placeholderText: "Internal Name"
                            Layout.preferredWidth: 120
                            background: Rectangle { color: "#333"; border.color: nameInput.activeFocus ? "#00ff00" : "#555" }
                            color: "white"
                        }

                        ComboBox {
                            id: fieldCombo
                            model: ["(None)"].concat(Array.from(availableFields || []))
                            Layout.fillWidth: true
                            Component.onCompleted: {
                                if (rowRoot.initialCsvField !== "") {
                                    let idx = find(rowRoot.initialCsvField);
                                    if (idx !== -1) currentIndex = idx;
                                }
                            }
                        }

                        Button {
                            text: "×"
                            onClicked: rowRoot.destroy()
                            Layout.preferredWidth: 30
                            palette.buttonText: "red"
                        }
                    }
                }

                Button {
                    text: "+ Add More Field"
                    Layout.fillWidth: true
                    onClicked: {
                        console.log("Adding new attribute row...");
                        attributeRowComponent.createObject(customAttributesList, {
                            "availableFields": modelController.availableFields
                        });
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

                Label { text: "Calculated Formulas (e.g. Volume):"; font.bold: true; font.pixelSize: 14 }
                
                ColumnLayout {
                    id: formulasList
                    Layout.fillWidth: true
                    spacing: 8
                }

                Button {
                    text: "＋ Add Custom Formula"
                    Layout.fillWidth: true
                    flat: false
                    onClicked: {
                        var component = Qt.createComponent("FormulaRow.qml");
                        if (component.status === Component.Ready) {
                            component.createObject(formulasList, {
                                "internalName": "Volume",
                                "expression": "XSPAN * YSPAN * ZSPAN",
                                "availableFields": modelController.availableFields
                            });
                        }
                    }
                }

                Item { Layout.preferredHeight: 20 }

                Button {
                    text: "Load Model"
                    Layout.fillWidth: true
                    highlighted: true
                    padding: 10
                    onClicked: {
                        let mapping = {
                            "X": comboX.currentText,
                            "Y": comboY.currentText,
                            "Z": comboZ.currentText
                        };
                        if (comboXS.currentText !== "(None)") mapping["XSPAN"] = comboXS.currentText;
                        if (comboYS.currentText !== "(None)") mapping["YSPAN"] = comboYS.currentText;
                        if (comboZS.currentText !== "(None)") mapping["ZSPAN"] = comboZS.currentText;

                        for (var i = 0; i < customAttributesList.children.length; i++) {
                            let row = customAttributesList.children[i];
                            if (row && row.internalName && row.csvField !== "(None)") {
                                console.log("Mapping custom attribute: " + row.internalName + " -> " + row.csvField);
                                mapping[row.internalName] = row.csvField;
                            }
                        }

                        let formulas = [];
                        for (var j = 0; j < formulasList.children.length; j++) {
                            let fRow = formulasList.children[j];
                            if (fRow && fRow.internalName && fRow.expression) {
                                formulas.push({
                                    "name": fRow.internalName,
                                    "expr": fRow.expression
                                });
                            }
                        }

                        mappingDialog.close()   // close first so isLoading=true prevents re-open
                        modelController.loadWithMapping(mapping, formulas)
                    }
                }
            }
        }
    }

    // Block properties panel
    BlockPropertyPanel {
        id: blockInfoPanel
        visible: false
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }

    // Analytics summary table
    AnalyticsPanel {
        id: analyticsPanel
    }

    View3D {
        id: view3d
        anchors.fill: parent

        environment: SceneEnvironment {
            clearColor: "#212121"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: blockModelNode

            Model {
                id: voxelModel
                source: "#Cube"
                scale: Qt.vector3d(1, 1, 1)
                pickable: true
                materials: [ PrincipledMaterial { baseColor: "white"; lighting: PrincipledMaterial.FragmentLighting } ]
                instancing: BlockModelProvider {
                    id: blockProvider
                    objectName: "blockProvider"
                    colorAttribute: "Grade"
                    blockSize: 1.0
                    gridMode: true
                }
            }

            // Selection highlight — one extra cube, zero instancing cost
            Model {
                id: selectionHighlight
                visible: blockInfoPanel.visible

                // Same Z-up → Y-up remap as getInstanceBuffer
                position: Qt.vector3d(
                    blockInfoPanel.blockData.X  || 0,
                    blockInfoPanel.blockData.Z  || 0,
                    -(blockInfoPanel.blockData.Y || 0)
                )

                // Match block dimensions, scaled 5% larger so it wraps around the block
                scale: Qt.vector3d(
                    (blockInfoPanel.blockData.X_span || 10) / 100 * blockProvider.blockSize * 1.05,
                    (blockInfoPanel.blockData.Z_span || 5)  / 100 * blockProvider.blockSize * 1.05,
                    (blockInfoPanel.blockData.Y_span || 10) / 100 * blockProvider.blockSize * 1.05
                )

                source: "#Cube"
                materials: [
                    PrincipledMaterial {
                        baseColor: Qt.rgba(1, 0.9, 0, 1)
                        emissiveFactor: Qt.vector3d(1, 0.8, 0)
                        lighting: PrincipledMaterial.NoLighting
                        cullMode: PrincipledMaterial.NoCulling
                        depthDrawMode: PrincipledMaterial.AlwaysDepthDraw
                        opacity: 0.6
                    }
                ]
            }
        }

        DirectionalLight { eulerRotation.x: -30; eulerRotation.y: -70 }
        PerspectiveCamera { id: mainCamera; position: Qt.vector3d(0, 0, 1000); lookAtNode: blockModelNode; clipFar: 100000; clipNear: 1 }
        
        OrbitCameraController {
            origin: blockModelNode
            camera: mainCamera
        }
    }

    // TapHandler uses Qt 6 pointer-event system — does not conflict with
    // OrbitCameraController's internal MouseArea (old event system).
    // gesturePolicy: DragThreshold cancels the tap if the mouse moves,
    // so orbit drags never accidentally trigger a pick.
    TapHandler {
        gesturePolicy: TapHandler.DragThreshold

        onTapped: (eventPoint) => {
            // eventPoint.position is already in the root item's coordinate space,
            // which matches view3d (anchors.fill: parent). No remapping needed.
            var pos = eventPoint.position;
            console.log("[Pick] Tapped at:", pos.x.toFixed(1), pos.y.toFixed(1));

            var result = view3d.pick(pos.x, pos.y);
            console.log("[Pick] objectHit=", result.objectHit, "| instanceIndex=", result.instanceIndex);

            if (result.objectHit && result.instanceIndex >= 0) {
                var info = blockProvider.getBlockInfo(result.instanceIndex);
                console.log("[Pick] Hit! keys:", Object.keys(info).join(", "));
                blockInfoPanel.blockData = info;
                blockInfoPanel.visible = true;
            } else {
                console.log("[Pick] No block hit.");
            }
        }
    }

    Rectangle {
        id: sidebar
        width: 250; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        color: Qt.rgba(0.1, 0.1, 0.1, 0.8); border.color: "#333"

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 15; spacing: 15
            Text { text: "Block Model Controller"; color: "white"; font.pixelSize: 16; font.bold: true }
            Button { text: "Open Model File"; Layout.fillWidth: true; onClicked: fileDialog.open() }
            Button {
                text: "Auto-Scale Camera"
                Layout.fillWidth: true
                enabled: modelController.status.includes("Loaded")
                onClicked: {
                    console.log("Auto-scaling camera. Radius: " + modelController.modelRadius);
                    mainCamera.position = Qt.vector3d(0, 0, modelController.modelRadius * 2.5)
                    mainCamera.lookAt(blockModelNode)
                }
            }

            Button {
                text: "Show Statistics"
                Layout.fillWidth: true
                visible: modelController.status.includes("Loaded")
                onClicked: analyticsPanel.open()
            }

            Text { text: "Visualization"; color: "white"; font.bold: true }
            
            CheckBox {
                text: "Render as Discrete Blocks"
                checked: true
                palette.windowText: "white"
                onCheckedChanged: blockProvider.gridMode = checked
            }

            Label { text: "Color By:"; color: "#aaa" }
            ComboBox {
                id: sidebarCombo
                Layout.fillWidth: true
                model: modelController.availableFields
                onCurrentTextChanged: blockProvider.colorAttribute = currentText
            }
            Label { text: "Rotation (Z-Axis):"; color: "#aaa" }
            Slider { 
                id: rotSlider; Layout.fillWidth: true; from: 0; to: 360; value: 0; 
                onValueChanged: blockProvider.setModelRotation(0, 0, value) 
            }

            Label { text: "Grade Cutoff:"; color: "#aaa" }
            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: gradeSlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: blockProvider.gradeMax
                    value: blockProvider.minGrade
                    onMoved: blockProvider.minGrade = value
                }
                TextField {
                    id: gradeInput
                    text: gradeSlider.value.toFixed(3)
                    Layout.preferredWidth: 60
                    color: "white"
                    font.pixelSize: 12
                    background: Rectangle { color: "#333"; border.color: "#555"; radius: 2 }
                    onEditingFinished: {
                        let val = parseFloat(text);
                        if (!isNaN(val)) {
                            blockProvider.minGrade = Math.max(0, Math.min(val, gradeSlider.to));
                        } else {
                            text = gradeSlider.value.toFixed(3);
                        }
                    }
                }
            }
            Text { text: "Max Data Grade: " + blockProvider.gradeMax.toFixed(2); color: "#888"; font.pixelSize: 10 }

            Item { Layout.fillHeight: true }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: modelController.isLoading

                ProgressBar {
                    id: loadProgress
                    Layout.fillWidth: true
                    // indeterminate while progress is unknown (e.g. .dat subprocess)
                    indeterminate: modelController.progress <= 0
                    value: modelController.progress
                    background: Rectangle {
                        implicitHeight: 6
                        color: "#333"
                        radius: 3
                    }
                    contentItem: Item {
                        Rectangle {
                            id: bar
                            width: loadProgress.indeterminate ? (parent.width * 0.3) : (loadProgress.visualPosition * parent.width)
                            height: parent.height
                            radius: 3
                            color: "#00ff88"
                            
                            NumberAnimation on x {
                                running: loadProgress.indeterminate
                                from: -bar.width
                                to: loadProgress.width
                                duration: 1000
                                loops: Animation.Infinite
                            }
                        }
                    }
                }
                Text {
                    text: modelController.progress === 0 ? "..." : (modelController.progress * 100).toFixed(0) + "%"
                    color: "#00ff88"
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignRight
                }
            }

            Rectangle {
                Layout.fillWidth: true; height: 40; color: "#333"; radius: 4
                Text {
                    anchors.centerIn: parent
                    text: modelController.status
                    color: modelController.isLoading ? "white" : "#00ff00"
                    font.family: "Monospace"; font.pixelSize: 10
                }
            }
        }
    }
}
