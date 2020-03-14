import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Window 2.12

Item {
    id: root

    Window.onWindowChanged: {
        if (Window.window) {
            Window.window.requestActivate()
            root.forceActiveFocus()
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