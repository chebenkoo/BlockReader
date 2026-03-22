import QtQuick
import QtQuick.Controls

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Mining Schedule - CGAL Integrated")

    Column {
        anchors.centerIn: parent
        spacing: 20

        Text {
            text: "CGAL Integration Successful!"
            font.pixelSize: 24
            color: "green"
        }

        Text {
            text: "Check 'Application Output' for the Point(1,2,3) log."
            font.pixelSize: 16
        }
    }
}
