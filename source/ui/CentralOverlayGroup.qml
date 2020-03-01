import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

Item {
	id: root
	anchors.fill: parent

	Item {
		id: logoHolder
		anchors.centerIn: parent
		width: logo.width
		height: logo.height
		Image {
			id: logo
			anchors.centerIn: parent
			source: "logo.webp"
		}
	}

	ColumnLayout {
		id: topColumn
		anchors.top: logoHolder.bottom
		anchors.left: parent.left
		anchors.right: parent.right
		spacing: 16

		CentralOverlayButton {
			id: someOverlayButton
			text: "News"
			leaningRight: true
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
			onExpansionFracChanged: logoHolder.opacity = 1.0 - someOverlayButton.expansionFrac
		}

		CentralOverlayButton {
			text: "Profile"
			leaningRight: true
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
		}

		CentralOverlayButton {
			text: "Play online"
			leaningRight: true
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
		}

		CentralOverlayButton {
			text: "Local game"
			leaningRight: true
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
		}
	}

	ColumnLayout {
		id: bottomColumn
		anchors.bottom: logoHolder.top
		anchors.left: parent.left
		anchors.right: parent.right
		spacing: 16

		CentralOverlayButton {
			text: "Settings"
			leaningRight: false
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
		}

		CentralOverlayButton {
			text: "Demos"
			leaningRight: false
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
		}

		CentralOverlayButton {
			text: "Help"
			leaningRight: false
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState()
		}

		CentralOverlayButton {
			text: "Quit"
			leaningRight: false
			Layout.fillWidth: true
			onClicked: root.toggleExpandedState(item)
		}
	}

	function toggleExpandedState(item) {
		for (let i = 0; i < topColumn.children.length; ++i) {
			topColumn.children[i].toggleExpandedState()
		}
		for (let i = 0; i < bottomColumn.children.length; ++i) {
			bottomColumn.children[i].toggleExpandedState()
		}
	}
}