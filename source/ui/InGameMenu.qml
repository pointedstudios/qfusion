import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import net.warsow 2.6

Rectangle {
    visible: wsw.isShowingInGameMenu
    anchors.fill: parent
    color: "#D8AA5500"

    Rectangle {
        Rectangle {
            width: parent.width
            anchors.top: parent.top
            color: Material.accentColor
            height: 3
        }

        width: 560
        height: 720
        anchors.centerIn: parent
        color: Material.backgroundColor

        layer.enabled: parent.enabled
        layer.effect: ElevationEffect { elevation: 32 }

        Button {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
            text: "Go back to main menu"
            flat: true
            onClicked: {
                wsw.showMainMenu()
            }
        }
    }

    Image {
        anchors.centerIn: parent
        source: "logo.webp"
    }

    Keys.onPressed: {
        if (!visible) {
            return
        }
        if (event.key !== Qt.Key_Escape) {
            return
        }

        event.accepted = true
        wsw.returnFromInGameMenu()
        mainMenu.forceActiveFocus()
    }
}