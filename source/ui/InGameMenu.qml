import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Rectangle {
    visible: wsw.isShowingInGameMenu
    anchors.fill: parent
    color: "#D8AA5500"

    readonly property real heightFrac: (Math.min(1080, rootItem.height - 720)) / (1080 - 720)

    Rectangle {
        anchors { left: parent.left; right: parent.right }
        height: tabBar.implicitHeight
        color: Material.backgroundColor

        TabBar {
            id: tabBar
            width: 1024
            anchors.horizontalCenter: parent.horizontalCenter
            background: null

            TabButton { text: "General" }
            TabButton { text: "Players" }
            TabButton { text: "Callvotes" }
        }
    }

    Rectangle {
        width: 480 + 120 * heightFrac
        height: 560 + 210 * heightFrac
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: +0.5 * tabBar.implicitHeight
        color: Material.backgroundColor
        radius: 3

        layer.enabled: parent.enabled
        layer.effect: ElevationEffect { elevation: 32 }

        StackLayout {
            anchors.fill: parent
            currentIndex: tabBar.currentIndex

            InGameGeneralPage {}
            InGamePlayersPage {}
            InGameCallvotesPage {}
        }
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