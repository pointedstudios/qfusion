import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Flickable {
    id: root
    flickableDirection: Flickable.VerticalFlick

    ColumnLayout {
        anchors {
            top: parent.top
            topMargin: rootItem.height > 800 ? 32 : 8
            horizontalCenter: parent.horizontalCenter
        }

        GridLayout {
            columns: 2
            columnSpacing: 16
            rowSpacing: 0 + 5 * (Math.min(1080, rootItem.height) - 720) / (1080 - 720)

            SettingsLabel {
                text: "Enable sound system"
            }

            CVarAwareCheckBox {
                cvarName: "s_module"
                applyImmediately: false
            }

            SettingsLabel {
                text: "Sound volume"
            }

            CVarAwareSlider {
                cvarName: "s_volume"
            }

            SettingsLabel {
                text: "Music volume"
            }

            CVarAwareSlider {
                cvarName: "s_musicvolume"
            }

            Label {
                visible: rootItem.height > 800
                Layout.alignment: Qt.AlignHCenter
                Layout.columnSpan: 2
                text: "Volume of in-game sounds"
                font.weight: Font.Medium
            }

            SettingsLabel {
                text: "Players sounds"
            }

            CVarAwareSlider {
                cvarName: "cg_volume_players"
            }

            SettingsLabel {
                text: "Effects sounds"
            }

            CVarAwareSlider {
                cvarName: "cg_volume_effects"
            }

            SettingsLabel {
                text: "Announcer sounds"
            }

            CVarAwareSlider {
                cvarName: "cg_volume_announcer"
            }

            SettingsLabel {
                text: "Hit beep sounds"
            }

            CVarAwareSlider {
                cvarName: "cg_volume_hitsound"
            }

            Label {
                visible: rootItem.height > 800
                Layout.alignment: Qt.AlignHCenter
                Layout.columnSpan: 2
                text: "Advanced effects"
                font.weight: Font.Medium
            }

            SettingsLabel {
                text: "Use environment effects"
            }

            CVarAwareCheckBox {
                cvarName: "s_environment_effects"
                applyImmediately: false
            }

            SettingsLabel {
                text: "Use HRTF"
            }

            CVarAwareCheckBox {
                cvarName: "s_hrtf"
                applyImmediately: false
            }

            Label {
                visible: rootItem.height > 800
                Layout.alignment: Qt.AlignHCenter
                Layout.columnSpan: 2
                text: "Miscellaneous settings"
                font.weight: Font.Medium
            }

            SettingsLabel {
                text: "Play sounds while in background"
            }

            CVarAwareCheckBox {
                cvarName: "s_globalfocus"
            }

            SettingsLabel {
                text: "Chat message sound"
            }

            CVarAwareCheckBox {
                cvarName: "cg_chatBeep"
            }

            SettingsLabel {
                text: "Heavy rocket explosions"
            }

            CVarAwareCheckBox {
                cvarName: "cg_heavyRocketExplosions"
            }

            SettingsLabel {
                text: "Heavy grenade explosions"
            }

            CVarAwareCheckBox {
                cvarName: "cg_heavyGrenadeExplosions"
            }

            SettingsLabel {
                text: "Heavy shockwave exploslions"
            }

            CVarAwareCheckBox {
                cvarName: "cg_heavyShockwaveExplosions"
            }
        }
    }
}
