import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    ColumnLayout {
        spacing: 16
        width: logo.width - 128
        anchors.bottom: logo.top
        anchors.bottomMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter

        InGameSelectorButton {
            text: "Main menu"
            onClicked: wsw.showMainMenu()
        }
        InGameSelectorButton {
            text: wsw.isSpectator ? "Join" : "Spectate"
        }
        InGameSelectorButton {
            text: "Gametype Options"
        }
    }

    Image {
        id: logo
        width: Math.min(implicitWidth, parent.width - 32)
        fillMode: Image.PreserveAspectFit
        anchors.centerIn: parent
        source: "logo.webp"
    }

    ColumnLayout {
        spacing: 16
        width: logo.width - 128
        anchors.top: logo.bottom
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter

        InGameSelectorButton {
            visible: wsw.isSpectator
            text: "Spectator options"
        }
        InGameSelectorButton {
            text: "Call a vote"
        }
        InGameSelectorButton {
            text: "Disconnect"
            onClicked: wsw.disconnect()
        }
    }
}