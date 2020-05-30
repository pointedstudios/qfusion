import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

RowLayout {
    id: root
    spacing: 4
    height: 28
    clip: false

    property real rowSpacing
    property var model

    Repeater {
        model: root.model

        delegate: KeyboardKey {
            rowSpacing: root.rowSpacing
            text: modelData["text"]
            quakeKey: modelData["quakeKey"]
            enabled: modelData["enabled"]
            hidden: modelData["hidden"]
            rowSpan: modelData["rowSpan"]
            group: modelData["group"]
            Layout.fillWidth: true
            Layout.preferredWidth: modelData["layoutWeight"]
        }
    }
}