import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12

Item {
	id: root
	height: 40

	property string text
	property bool leaningRight: false
	property real expansionFrac: 0.0

	signal clicked(var item)

	function toggleExpandedState() {
		if (state == "centered") {
			state = leaningRight ? "pinnedToLeft" : "pinnedToRight"
		} else {
			state = "centered"
		}
	}

	readonly property var transformMatrix: Qt.matrix4x4(
		+1.0, -0.3, +0.0, +0.0,
		+0.0, +1.0, +0.0, +0.0,
		+0.0, +0.0, +1.0, +0.0,
		+0.0, +0.0, +0.0, +1.0)

	property color foregroundColor:
		Qt.lighter(Material.backgroundColor, 1.5)
	property color trailDecayColor:
		Qt.rgba(Material.backgroundColor.r, Material.backgroundColor.g, Material.backgroundColor.b, 0)
	property color highlightedColor: "orange"

	states: [
		State {
			name: "centered"
			AnchorChanges {
				target: contentRow
				anchors.horizontalCenter: root.horizontalCenter
				anchors.left: undefined
				anchors.right: undefined
			}
		},
		State {
			name: "pinnedToLeft"
			AnchorChanges {
				target: contentRow
				anchors.horizontalCenter: undefined
				anchors.left: root.left
				anchors.right: undefined
			}
		},
		State {
			name: "pinnedToRight"
			AnchorChanges {
				target: contentRow
				anchors.horizontalCenter: undefined
				anchors.left: undefined
				anchors.right: root.right
			}
		}
	]

	transitions: Transition {
		AnchorAnimation {
			duration: 200
		}
	}

	state: "centered"

	CentralOverlayButtonTrail {
		visible: !leaningRight
		anchors.right: contentRow.left
		anchors.rightMargin: 4
		leftColor: mouseArea.containsMouse ? highlightedColor : root.foregroundColor
		rightColor: root.trailDecayColor
		transformMatrix: root.transformMatrix
	}

	Rectangle {
		id: contentRow
		height: 40
		width: 224
		radius: 2
		color: mouseArea.containsMouse ? highlightedColor : foregroundColor

		transform: Matrix4x4 {
			matrix: root.transformMatrix
		}

		onXChanged: {
			let halfContainerWidth = parent.width / 2
			let halfThisWidth = width / 2
			let slidingDistance = halfContainerWidth - halfThisWidth
			if (root.leaningRight) {
				root.expansionFrac = Math.abs(x - halfContainerWidth + halfThisWidth) / slidingDistance
			} else {
				root.expansionFrac = Math.abs(x - halfContainerWidth + halfThisWidth) / slidingDistance
			}
		}

		Label {
			anchors.left: root.leaningRight ? parent.left: undefined
			anchors.right: root.leaningRight ? undefined: parent.right
			anchors.verticalCenter: parent.verticalCenter
			anchors.leftMargin: 12
			anchors.rightMargin: 12
			font.pointSize: 20
			text: root.text
			font.weight: Font.Bold
			font.capitalization: Font.AllUppercase
		}

		MouseArea {
			id: mouseArea
			anchors.fill: parent
			hoverEnabled: true
			onClicked: root.clicked(root)
		}
	}

	CentralOverlayButtonTrail {
		anchors.left: contentRow.right
		anchors.leftMargin: 4
		visible: leaningRight
		leftColor: root.trailDecayColor
		rightColor: mouseArea.containsMouse ? highlightedColor : root.foregroundColor
		transformMatrix: root.transformMatrix
	}
}
