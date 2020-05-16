import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: quitPage
    property var backTrigger

    Popup {
        id: popup
        modal: true
        focus: true
        dim: false
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 280
        height: 220

        function openSelf() {
            popup.parent = rootItem.windowContentItem
            rootItem.enablePopupOverlay()
            popup.open()
        }

        function closeSelf() {
            if (!opened) {
                return
            }

            popup.close()
            rootItem.disablePopupOverlay()
            quitPage.backTrigger()
        }

        background: Rectangle {
            Rectangle {
                width: parent.width
                height: 3
                anchors.top: parent.top
                color: Material.accentColor
            }

            width: parent.width
            height: parent.height
            focus: true
            radius: 3
            color: Material.backgroundColor
            layer.enabled: parent.enabled
            layer.effect: ElevationEffect { elevation: 32 }

            Keys.onPressed: {
                if (event.key === Qt.Key_Escape) {
                    popup.closeSelf()
                }
            }
        }

        Label {
            anchors.top: parent.top
            anchors.topMargin: 32
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Quit the game?"
            font.weight: Font.Light
            font.pointSize: 15
        }

        Button {
            id: yesButton
            anchors { bottom: parent.bottom; left: parent.left }
            text: "Yes"
            flat: true
            onClicked: wsw.quit()
        }

        Button {
            anchors { bottom: parent.bottom; right: parent.right }
            text: "No"
            flat: true
            highlighted: true
            onClicked: popup.closeSelf()
        }
    }

    Component.onCompleted: popup.openSelf()
    Component.onDestruction: popup.closeSelf()
}