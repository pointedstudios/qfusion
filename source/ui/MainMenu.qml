import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    anchors.fill: parent
    visible: wsw.quakeClientState === QuakeClient.Disconnected
    opacity: 0.98

    Rectangle {
        anchors.fill: parent
        color: Material.backgroundColor
    }

    CentralMenuGroup {}
}