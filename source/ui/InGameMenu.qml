import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    visible: wsw.quakeClientState === QuakeClient.Active
    anchors.fill: parent
    opacity: 0.98

    Rectangle {
        width: 800
        height: 320
        anchors.centerIn: parent
        color: Material.backgroundColor
        border.width: 2
        border.color: Material.accentColor
    }

    Image {
        anchors.centerIn: parent
        source: "logo.webp"
    }
}