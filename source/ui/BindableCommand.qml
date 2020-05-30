import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

MouseArea {
    id: root

    property string text
    property string command
    property int commandNum
    property int commandGroup

    property real defaultLabelWidth
    property real defaultLabelHeight

    hoverEnabled: true
    implicitHeight: Math.max(marker.height, defaultLabelHeight) + 4
    implicitWidth: marker.defaultWidth + defaultLabelWidth + 12

    Component.onCompleted: keysAndBindings.registerCommandItem(root, commandNum)
    Component.onDestruction: keysAndBindings.unregisterCommandItem(root, commandNum)

    onClicked: keysAndBindings.onCommandItemClicked(root, commandNum)

    Label {
        id: marker
        text: "\u2716"
        readonly property real defaultWidth: width
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
    }

    Label {
        id: label
        text: root.text
        font.pointSize: root.containsMouse ? 12 : 11
        font.weight: root.containsMouse ? Font.Medium : Font.Normal
        anchors {
            left: marker.right
            leftMargin: 12
            verticalCenter: parent.verticalCenter
        }

        Component.onCompleted: {
            root.defaultLabelWidth = label.implicitWidth
            root.defaultLabelHeight = label.implicitHeight
        }
    }
}
