import QtQuick 2.12
import QtQuick.Controls 2.12
import net.warsow 2.6

Item {
    id: root

    property int nativeZ
    property string modelName
    property string skinName
    property vector3d modelOrigin
    property vector3d rotationAxis
    property real rotationSpeed: 0.0
    property vector3d viewOrigin
    property real viewFov: 90.0

    implicitWidth: underlying.isLoaded ? 0 : 192
    implicitHeight: underlying.isLoaded ? 0 : 192

    NativelyDrawnModel_Native {
        id: underlying
        nativeZ: root.nativeZ
        modelName: root.modelName
        skinName: root.skinName
        modelOrigin: root.modelOrigin
        rotationAxis: root.rotationAxis
        rotationSpeed: root.rotationSpeed
        viewOrigin: root.viewOrigin
        viewFov: root.viewFov
        width: root.width
        height: root.height
        anchors.centerIn: parent

        Component.onCompleted: wsw.registerNativelyDrawnItem(underlying)
        Component.onDestruction: wsw.unregisterNativelyDrawnItem(underlying)
    }

    Loader {
        anchors.fill: parent
        sourceComponent: underlying.isLoaded && !wsw.isDebuggingNativelyDrawnItems ? null : debuggingPlaceholder
    }

    Component {
        id: debuggingPlaceholder

        Rectangle {
            anchors.fill: parent
            color: "orange"
            opacity: 0.125
            border.width: 1
            border.color: "red"

            Label {
                anchors.centerIn: parent
                wrapMode: Text.NoWrap
                font.pointSize: 12
                text: skinName.length > 0 ? modelName + " " + skinName : modelName
            }
        }
    }
}