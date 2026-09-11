import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: "Writero"

    palette.window: "#141414"
    palette.windowText: "#e8e6e3"
    palette.base: "#1c1c1c"
    palette.text: "#e8e6e3"
    palette.highlight: "#6c7a5b"
    palette.highlightedText: "#ffffff"
    color: palette.window

    Label {
        anchors.centerIn: parent
        color: root.palette.windowText
        text: qsTr("Writero desktop is starting…")
    }
}
