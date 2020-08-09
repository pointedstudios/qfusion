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
        focus: true
        width: 480 + 120 * heightFrac
        height: 560 + 210 * heightFrac
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        color: Material.backgroundColor
        radius: 3

        layer.enabled: parent.enabled
        layer.effect: ElevationEffect { elevation: 64 }

        StackView {
            id: stackView
            anchors.fill: parent
            anchors.margins: 16
            focus: true
            initialItem: selectorComponent
        }

        Component {
            id: selectorComponent
            InGameSelectorPage {}
        }
    }

    onVisibleChanged: {
        if (visible) {
            stackView.forceActiveFocus()
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
        if (stackView.depth === 1) {
            wsw.returnFromInGameMenu()
            mainMenu.forceActiveFocus()
            return
        }

        let handler = stackView.currentItem.handleKeyBack
        if (handler && handler()) {
            return
        }

        // .pop() API quirks
        stackView.pop(stackView.get(stackView.depth - 2))
    }
}