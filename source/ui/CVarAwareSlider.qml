import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Slider {
    id: root

    Material.theme: Material.Dark
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true

    function checkCVarChanges() {
        let newValue = wsw.getCVarValue(cvarName)
        if (value != newValue) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                value = newValue
            }
        }
    }

    function rollbackChanges() {
        value = wsw.getCVarValue(cvarName)
    }

    onValueChanged: {
        if (applyImmediately) {
            wsw.setCVarValue(cvarName, value)
        } else {
            wsw.markPendingCVarChanges(root, cvarName, value)
        }
    }

    Component.onCompleted: {
        value = wsw.getCVarValue(cvarName)
        wsw.registerCVarAwareControl(root)
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)
}