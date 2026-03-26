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
                scale: Qt.vector3d(0.1, 0.1, 0.1)
                materials: [ PrincipledMaterial { baseColor: "white"; lighting: PrincipledMaterial.NoLighting } ]
                instancing: BlockModelProvider {
                    id: blockProvider
                    objectName: "blockProvider"
                    colorAttribute: "Grade"
                    blockSize: 1.0
                }
            }
        }

        DirectionalLight { eulerRotation.x: -30; eulerRotation.y: -70 }
        PerspectiveCamera { id: mainCamera; position: Qt.vector3d(0, 0, 1000); lookAtNode: blockModelNode; clipFar: 100000; clipNear: 1 }
        
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
                    mainCamera.position = Qt.vector3d(0, 0, 2000)
                    mainCamera.lookAt(blockModelNode)
                }
            }
            Text { text: "Visualization"; color: "white"; font.bold: true }
            Label { text: "Color By:"; color: "#aaa" }
            ComboBox {
                Layout.fillWidth: true
                model: modelController.availableFields
                // Default to Grade or AuCut so the colour is meaningful on first load
                currentIndex: {
                    var g = model.indexOf("Grade");
                    if (g >= 0) return g;
                    var a = model.indexOf("AuCut");
                    if (a >= 0) return a;
                    return 0;
                }
                onCurrentTextChanged: blockProvider.colorAttribute = currentText
            }
            Label { text: "Block Size:"; color: "#aaa" }
            Slider { Layout.fillWidth: true; from: 0.1; to: 20.0; value: 0.8; onValueChanged: blockProvider.blockSize = value }

            Label { text: "Grade Cutoff:"; color: "#aaa" }
            Slider { id: gradeSlider; Layout.fillWidth: true; from: 0.0; to: 5.0; value: 0.0; onValueChanged: blockProvider.minGrade = value }
            Text { text: "Min: " + gradeSlider.value.toFixed(2); color: "white"; font.pixelSize: 10 }

            Item { Layout.fillHeight: true }
            Rectangle {
                Layout.fillWidth: true; height: 40; color: "#333"; radius: 4
                Text { anchors.centerIn: parent; text: modelController.status; color: "#00ff00"; font.family: "Monospace"; font.pixelSize: 10 }
            }
        }
    }
}
