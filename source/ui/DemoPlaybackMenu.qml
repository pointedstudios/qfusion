import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    visible: wsw.isPlayingADemo
    height: 240
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.leftMargin: parent.width / 5
    anchors.rightMargin: parent.width / 5
    opacity: 0.98

    Rectangle {
        anchors.fill: parent
        color: Material.backgroundColor
        border.width: 2
        border.color: Material.backgroundColor
    }
}