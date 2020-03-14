import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtGraphicalEffects 1.12
import net.warsow 2.6

Item {
	id: root
	anchors.fill: parent
	visible: wsw.quakeClientState === QuakeClient.Disconnected

	Item {
		id: contentPane
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		anchors.horizontalCenter: parent.horizontalCenter
		width: 1024
	}

	readonly property real contentPaneMargin:
		0.5 * (root.width - contentPane.width)
	readonly property real centralPaneGradientFrac:
		contentPaneMargin / root.width + 0.125 * contentPane.width / root.width

	readonly property color tintColor:
		Qt.rgba(Material.accentColor.r, Material.accentColor.g, Material.accentColor.b, 0.05)
	readonly property color backgroundColor:
		Qt.rgba(Material.backgroundColor.r, Material.backgroundColor.g, Material.backgroundColor.b, 0.97)
	readonly property color baseGlowColor:
		Qt.tint(Material.backgroundColor, tintColor)
	readonly property color centralPaneColor:
		Qt.rgba(baseGlowColor.r, baseGlowColor.g, baseGlowColor.b, 0.97)

	Item {
		id: centered
		anchors.fill: parent
		opacity: Math.sqrt(Math.sqrt((1.0 - centralOverlay.expansionFrac)))
		RadialGradient {
			anchors.fill: parent
			gradient: Gradient {
				GradientStop {
					position: 0.0;
					color: Qt.rgba(baseGlowColor.r, baseGlowColor.g, baseGlowColor.b, 0.95)
				}
				GradientStop {
					position: 1.0;
					color: backgroundColor
				}
			}
		}
	}

	Item {
		anchors.fill: parent
		opacity: Math.sqrt(Math.sqrt(centralOverlay.expansionFrac, 4.0))
		LinearGradient {
			anchors.fill: parent
			start: Qt.point(0, 0)
			end: Qt.point(parent.width, 0)

			gradient: Gradient {
				GradientStop {
					position: 0.0
					color: backgroundColor
				}
				GradientStop {
					position: centralPaneGradientFrac
					color: centralPaneColor
				}
				GradientStop {
					position: 1.0 - centralPaneGradientFrac
					color: centralPaneColor
				}
				GradientStop {
					position: 1.0
					color: backgroundColor
				}
			}
		}
	}

	CentralOverlayGroup {
		id: centralOverlay
	}

	Keys.onPressed: {
	    if (!centralOverlay.handleKeyEvent(event)) {
	        if (event.key === Qt.Key_Escape) {
	            root.forceActiveFocus()
	            event.accepted = true
	        }
	    }
	}
}