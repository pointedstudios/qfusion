import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Material.impl 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root
    Component.onCompleted: wsw.startServerListUpdates()
    Component.onDestruction: wsw.stopServerListUpdates()

    TableView {
        id: tableView
        anchors.fill: parent
        columnSpacing: 28
        rowSpacing: 40
        interactive: true
        flickableDirection: Flickable.VerticalFlick

        model: serverListModel

        delegate: ServerBrowserCard {
            implicitWidth: root.width / 2
            visible: typeof(serverName) !== "undefined"
            serverName: model["serverName"] || ""
            mapName: model["mapName"] || ""
            gametype: model["gametype"] || ""
            address: model["address"] || ""
            numPlayers: model["numPlayers"] || 0
            maxPlayers: model["maxPlayers"] || 0
            timeMinutes: model["timeMinutes"]
            timeSeconds: model["timeSeconds"]
            timeFlags: model["timeFlags"]
            alphaTeamName: model["alphaTeamName"]
            betaTeamName: model["betaTeamName"]
            alphaTeamScore: model["alphaTeamScore"]
            betaTeamScore: model["betaTeamScore"]
            alphaTeamList: model["alphaTeamList"]
            betaTeamList: model["betaTeamList"]
            playersTeamList: model["playersTeamList"]
            spectatorsList: model["spectatorsList"]

            onImplicitHeightChanged: forceLayoutTimer.start()
        }

        Timer {
            id: forceLayoutTimer
            interval: 1
            onTriggered: tableView.forceLayout()
        }
    }
}