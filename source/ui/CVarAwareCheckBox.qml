import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

CheckBox {
    id: root

    Material.theme: Material.Dark
    Material.foreground: "white"
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true

    function checkCVarChanges() {
        let value = wsw.getCVarValue(cvarName) != 0
        if (checked != value) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                checked = value
            }
        }
    }

    function rollbackChanges() {
        checked = wsw.getCVarValue(cvarName) != 0
    }

    onClicked: {
        let value = checked ? "1" : "0"
        if (applyImmediately) {
            wsw.setCVarValue(cvarName, value)
        } else {
            wsw.markPendingCVarChanges(root, cvarName, value)
        }
    }

    Component.onCompleted: {
        checked = wsw.getCVarValue(cvarName) != 0
        wsw.registerCVarAwareControl(root)
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)
}