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

    Dialog {
        id: mappingDialog
        title: "Map Block Model Fields"
        width: 450
        modal: true
        anchors.centerIn: parent
        visible: modelController.availableFields.length > 0 && !modelController.status.includes("Loaded")

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label { text: "Map Spatial Coordinates (Centroids):"; font.bold: true }
            RowLayout {
                Label { text: "X:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboX; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("EAST")); Layout.fillWidth: true }
            }
            RowLayout {
                Label { text: "Y:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboY; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("NORTH")); Layout.fillWidth: true }
            }
            RowLayout {
                Label { text: "Z:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboZ; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("RL")); Layout.fillWidth: true }
            }

            Label { text: "Map Block Spans (Half-Widths):"; font.bold: true }
            RowLayout {
                Label { text: "X Span:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboXS; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("_EAST")); Layout.fillWidth: true }
            }
            RowLayout {
                Label { text: "Y Span:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboYS; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("_NORTH")); Layout.fillWidth: true }
            }
            RowLayout {
                Label { text: "Z Span:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboZS; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("_RL")); Layout.fillWidth: true }
            }

            Label { text: "Map Attributes:"; font.bold: true }
            RowLayout {
                Label { text: "Grade:"; Layout.preferredWidth: 50 }
                ComboBox { id: comboGrade; model: modelController.availableFields; currentIndex: Math.max(0, model.indexOf("AuCut")); Layout.fillWidth: true }
            }

            Button {
                text: "Load Model"
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    modelController.loadWithMapping({
                        "X": comboX.currentText,
                        "Y": comboY.currentText,
                        "Z": comboZ.currentText,
                        "XSPAN": comboXS.currentText,
                        "YSPAN": comboYS.currentText,
                        "ZSPAN": comboZS.currentText,
                        "Grade": comboGrade.currentText
                    })
                    mappingDialog.close()
                }
            }
        }
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
                // #Cube in Qt Quick 3D is 100×100×100 units.
                // Dividing by 100 makes it a 1×1×1 unit cube so that instance
                // scale values (which are in metres) map 1-to-1.
                scale: Qt.vector3d(0.01, 0.01, 0.01)
                materials: [
                    PrincipledMaterial {
                        baseColor: "white"
                        lighting: PrincipledMaterial.NoLighting
                        opacity: 0.95
                    }
                ]
                instancing: BlockModelProvider {
                    id: blockProvider
                    objectName: "blockProvider"
                    colorAttribute: "Grade"
                    blockSize: 0.8 // Default to 80% to see the grid structure
                }
            }
        }

        DirectionalLight { eulerRotation.x: -30; eulerRotation.y: -70 }
        
        PerspectiveCamera { 
            id: mainCamera
            position: Qt.vector3d(modelController.modelRadius, modelController.modelRadius, modelController.modelRadius)
            lookAtNode: blockModelNode
            clipFar: 500000
            clipNear: 10 // Increase near clip to prevent inside-block artifacts
        }
        
        OrbitCameraController { 
            origin: blockModelNode
            camera: mainCamera
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
                    var dist = Math.max(1000, modelController.modelRadius * 1.5)
                    mainCamera.position = Qt.vector3d(dist, dist, dist)
                    mainCamera.lookAt(blockModelNode)
                }
            }
            Text { text: "Visualization"; color: "white"; font.bold: true }
            Label { text: "Color By:"; color: "#aaa" }
            ComboBox {
                id: colorAttrCombo
                Layout.fillWidth: true
                model: modelController.availableFields
                onCurrentTextChanged: blockProvider.colorAttribute = currentText
            }
            Label { text: "Block Size (Scale):"; color: "#aaa" }
            Slider { 
                id: sizeSlider
                Layout.fillWidth: true; from: 0.01; to: 2.0; value: 0.8; 
                onValueChanged: blockProvider.blockSize = value 
            }

            Label { text: "Grade Cutoff:"; color: "#aaa" }
            Slider { id: gradeSlider; Layout.fillWidth: true; from: 0.0; to: 10.0; value: 0.0; onValueChanged: blockProvider.minGrade = value }
            Text { text: "Min: " + gradeSlider.value.toFixed(2); color: "white"; font.pixelSize: 10 }

            Item { Layout.fillHeight: true }
            Rectangle {
                Layout.fillWidth: true; height: 40; color: "#333"; radius: 4
                Text { anchors.centerIn: parent; text: modelController.status; color: "#00ff00"; font.family: "Monospace"; font.pixelSize: 10 }
            }
        }
    }
}
