import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Item {
    id: root
    implicitHeight: parent.height
    clip: false

    property string text: ""
    property int quakeKey: -1
    property int rowSpan: 1
    property int group: 0
    property real rowSpacing
    property bool hidden: false

    Rectangle {
        color: !hidden ? Qt.rgba(0, 0, 0, 0.3) : "transparent"
        radius: 5
        width: parent.width
        height: parent.height * rowSpan + rowSpacing * (rowSpan - 1)

        border {
            width: mouseArea.containsMouse ? 2 : 0
            color: Material.accentColor
        }
        anchors {
            top: parent.top
            left: parent.left
        }

        Component.onCompleted: keysAndBindings.registerKeyItem(root, quakeKey)
        Component.onDestruction: keysAndBindings.unregisterKeyItem(root, quakeKey)

        Rectangle {
            anchors {
                left: parent.left
                leftMargin: 5
                verticalCenter: parent.verticalCenter
            }
            width: 5
            height: 5
            radius: 2.5
            visible: !!root.group && root.visible && root.enabled
            color: keysAndBindings.colorForGroup(root.group)
        }

        MouseArea {
            id: mouseArea
            enabled: !root.hidden && root.enabled
            hoverEnabled: true
            anchors.centerIn: parent
            width: parent.width - 10
            height: parent.height - 10
        }

        Label {
            anchors.centerIn: parent
            text: !root.hidden ? root.text : ""
            font.pointSize: 11
            font.weight: Font.Medium
            color: Material.foreground
        }
    }
}