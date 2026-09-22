import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 356
    height: 86
    visible: false
    color: "transparent"
    title: "Mango OSD"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus

    property var colors: themeColors
    property string osdKind: "volume"
    property int osdValue: 0
    property bool osdMuted: false
    readonly property string label: osdKind === "brightness"
                                     ? "Brightness"
                                     : (osdMuted ? "Muted" : "Volume")
    readonly property string icon: osdKind === "brightness"
                                    ? "󰃠"
                                    : (osdMuted || osdValue === 0
                                       ? "󰝟"
                                       : (osdValue < 50 ? "󰕿" : "󰕾"))

    function showOsd() {
        hideAnimation.stop()
        hideTimer.restart()
        if (root.visible) {
            panel.opacity = 1
            return
        }
        panel.opacity = 0
        root.visible = true
        entrance.restart()
    }

    Timer {
        id: hideTimer
        interval: 1050
        repeat: false
        onTriggered: hideAnimation.restart()
    }

    NumberAnimation {
        id: entrance
        target: panel
        property: "opacity"
        from: 0
        to: 1
        duration: 110
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id: hideAnimation
        target: panel
        property: "opacity"
        from: 1
        to: 0
        duration: 130
        easing.type: Easing.InCubic
        onFinished: root.visible = false
    }

    Rectangle {
        id: panel
        opacity: 0
        anchors.fill: parent
        radius: 24
        color: colors.surfaceContainerHigh
        border.width: 1
        border.color: colors.outlineVariant

        Row {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 14

            Rectangle {
                width: 46
                height: 46
                anchors.verticalCenter: parent.verticalCenter
                radius: 23
                color: colors.primaryContainer

                Text {
                    anchors.centerIn: parent
                    text: root.icon
                    color: colors.onPrimaryContainer
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 21
                }
            }

            Item {
                width: parent.width - 60
                height: parent.height

                Text {
                    id: titleLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    text: root.label
                    color: colors.onSurface
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.top: titleLabel.top
                    text: root.osdValue + "%"
                    color: colors.primary
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 7
                    height: 5
                    radius: 3
                    color: colors.outlineVariant

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(100, root.osdValue)) / 100
                        height: parent.height
                        radius: parent.radius
                        color: colors.primary

                        Behavior on width {
                            NumberAnimation {
                                duration: 110
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }
            }
        }
    }
}
