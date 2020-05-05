import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12

Item {
    id: rootItem

    property var windowContentItem

    Window.onWindowChanged: {
        if (Window.window) {
            Window.window.requestActivate()
            rootItem.forceActiveFocus()
            rootItem.windowContentItem = Window.window.contentItem
        }
    }

    Keys.forwardTo: [mainMenu, demoPlaybackMenu, respectTokensMenu, inGameMenu]

    MainMenu {
        id: mainMenu
    }
    DemoPlaybackMenu {
        id: demoPlaybackMenu
    }
    RespectTokensMenu {
        id: respectTokensMenu
    }
    InGameMenu {
        id: inGameMenu
    }
}