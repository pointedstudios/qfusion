import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    id: root

    Component.onCompleted: keysAndBindings.reload()

    KeyboardPane {
        id: mainKeyboardPane
        anchors {
            top: parent.top
            topMargin: 32
            horizontalCenter: parent.horizontalCenter
        }

        width: root.width - 96
        rowHeight: 26
        rowModels: [
            keysAndBindings.keyboardMainPadRow1,
            keysAndBindings.keyboardMainPadRow2,
            keysAndBindings.keyboardMainPadRow3,
            keysAndBindings.keyboardMainPadRow4,
            keysAndBindings.keyboardMainPadRow5,
            keysAndBindings.keyboardMainPadRow6
        ]
    }

    MouseKeysPane {
        height: arrowPadPane.height
        anchors {
            top: mainKeyboardPane.bottom
            topMargin: 24
            left: mainKeyboardPane.left
            right: arrowPadPane.left
            rightMargin: 32
        }
    }

    KeyboardPane {
        id: arrowPadPane
        width: mainKeyboardPane.width / 5

        anchors {
            top: mainKeyboardPane.bottom
            topMargin: 24
            right: numPadPane.left
            rightMargin: 32
        }

        rowHeight: 26
        rowModels: [
            keysAndBindings.keyboardArrowPadRow1,
            keysAndBindings.keyboardArrowPadRow2,
            keysAndBindings.keyboardArrowPadRow3,
            keysAndBindings.keyboardArrowPadRow4,
            keysAndBindings.keyboardArrowPadRow5
        ]
    }

    KeyboardPane {
        id: numPadPane
        anchors {
            top: mainKeyboardPane.bottom
            topMargin: 24
            right: mainKeyboardPane.right
        }

        // One extra key
        width: 4 * arrowPadPane.width / 3

        rowHeight: 26
        rowModels: [
            keysAndBindings.keyboardNumPadRow1,
            keysAndBindings.keyboardNumPadRow2,
            keysAndBindings.keyboardNumPadRow3,
            keysAndBindings.keyboardNumPadRow4,
            keysAndBindings.keyboardNumPadRow5,
        ]
    }

    RowLayout {
        spacing: 32
        anchors {
            top: numPadPane.bottom
            topMargin: 48
            horizontalCenter: parent.horizontalCenter
        }

        ColumnLayout {
            width: movementColumn.width
            Layout.alignment: Qt.AlignTop
            spacing: 16

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Movement"
                font.capitalization: Font.AllUppercase
            }

            Rectangle {
                height: 3; width: movementColumn.width
                color: keysAndBindings.colorForGroup(KeysAndBindings.MovementGroup)
            }

            BindableCommandsColumn {
                id: movementColumn
                Layout.alignment: Qt.AlignTop
                model: keysAndBindings.commandsMovementColumn
            }
        }

        ColumnLayout {
            width: weaponColumnsRow.width
            Layout.alignment: Qt.AlignTop
            spacing: 16

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Weapons"
                font.capitalization: Font.AllUppercase
            }

            Rectangle {
                height: 3; width: weaponColumnsRow.width
                color: keysAndBindings.colorForGroup(KeysAndBindings.WeaponGroup)
            }

            RowLayout {
                id: weaponColumnsRow
                spacing: 40

                BindableCommandsColumn {
                    Layout.alignment: Qt.AlignTop
                    model: keysAndBindings.commandsWeaponsColumn1
                }

                BindableCommandsColumn {
                    Layout.alignment: Qt.AlignTop
                    model: keysAndBindings.commandsWeaponsColumn2
                }
            }
        }

        ColumnLayout {
            width: actionsColumn.width
            Layout.alignment: Qt.AlignTop
            spacing: 16

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Actions"
                font.capitalization: Font.AllUppercase
            }

            Rectangle {
                height: 3; width: actionsColumn.width
                color: keysAndBindings.colorForGroup(KeysAndBindings.ActionGroup)
            }

            BindableCommandsColumn {
                id: actionsColumn
                Layout.alignment: Qt.AlignTop
                model: keysAndBindings.commandsActionsColumn
            }
        }

        ColumnLayout {
            width: respectColumnsRow.width
            Layout.alignment: Qt.AlignTop
            spacing: 16

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "R&S Tokens"
                font.capitalization: Font.AllUppercase
            }

            Rectangle {
                height: 3; width: respectColumnsRow.width
                color: keysAndBindings.colorForGroup(KeysAndBindings.RespectGroup)
            }

            RowLayout {
                id: respectColumnsRow
                spacing: 40

                BindableCommandsColumn {
                    Layout.alignment: Qt.AlignTop
                    model: keysAndBindings.commandsRespectColumn1
                }

                BindableCommandsColumn {
                    Layout.alignment: Qt.AlignTop
                    model: keysAndBindings.commandsRespectColumn2
                }
            }
        }
    }
}
