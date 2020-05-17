import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    Image {
        width: Math.min(implicitWidth, parent.width - 32)
        fillMode: Image.PreserveAspectFit
        anchors.centerIn: parent
        source: "logo.webp"
    }
}