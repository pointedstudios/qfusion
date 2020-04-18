import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

TextField {
    id: root

    Material.theme: Material.Dark
    Material.accent: "orange"

    property string cvarName: ""
    property bool applyImmediately: true

    function checkCVarChanges() {
        let actualValue = wsw.getCVarValue(cvarName)
        if (actualValue != text) {
            if (applyImmediately || !wsw.hasControlPendingCVarChanges(root)) {
                text = actualValue
            }
        }
    }

    function rollbackChanges() {
        text = wsw.getCVarValue(cvarName)
    }

    onTextEdited: {
        if (applyImmediately) {
            wsw.setCVarValue(cvarName, text)
        } else {
            wsw.markPendingCVarChanges(root, cvarName, text)
        }
    }

    Component.onCompleted: {
        text = wsw.getCVarValue(cvarName)
        wsw.registerCVarAwareControl(root)
    }

    Component.onDestruction: wsw.unregisterCVarAwareControl(root)
}