import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    width: 420
    height: 510
    x: Screen.width - width - 8
    y: 46
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    title: "bluetooth-popup"

    property color primary: themeColors.primary
    property color onPrimary: themeColors.onPrimary
    property color surface: themeColors.surface
    property color raised: themeColors.surfaceContainer
    property color raisedHigh: themeColors.surfaceContainerHigh
    property color text: themeColors.onSurface
    property color muted: themeColors.onSurfaceVariant
    property color outline: themeColors.outlineVariant
    property bool everActive: false

    palette.window: surface
    palette.button: raisedHigh
    palette.buttonText: text
    palette.base: raised
    palette.text: text
    palette.highlight: primary
    palette.highlightedText: onPrimary

    Component.onCompleted: activationTimer.start()
    onActiveChanged: {
        if (active) everActive = true
        else if (everActive && visible) closeTimer.restart()
    }
    Timer { id: activationTimer; interval: 180; onTriggered: root.requestActivate() }
    Timer { id: closeTimer; interval: 300; onTriggered: if (!root.active) Qt.quit() }

    Shortcut { sequence: "Esc"; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Up"
        enabled: deviceList.count > 0
        onActivated: deviceList.currentIndex = Math.max(0, deviceList.currentIndex - 1)
    }
    Shortcut {
        sequence: "Down"
        enabled: deviceList.count > 0
        onActivated: deviceList.currentIndex = Math.min(deviceList.count - 1, deviceList.currentIndex + 1)
    }
    Shortcut {
        sequence: "Return"
        enabled: deviceList.currentIndex >= 0 && !bluetoothController.busy
        onActivated: bluetoothController.activate(deviceList.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: deviceList.currentIndex >= 0 && !bluetoothController.busy
        onActivated: bluetoothController.activate(deviceList.currentIndex)
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: root.surface
        antialiasing: true
        border.color: root.outline
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                Label { text: "Bluetooth"; color: root.primary; font.bold: true; font.pixelSize: 15 }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: bluetoothController.busy
                    visible: running
                    Layout.preferredWidth: 25
                    Layout.preferredHeight: 25
                }
                Label {
                    text: bluetoothController.status
                    color: root.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.maximumWidth: 210
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    text: "Scan"
                    enabled: !bluetoothController.busy
                    onClicked: bluetoothController.scan()
                }
                Button {
                    text: bluetoothController.powered ? "Turn off" : "Turn on"
                    enabled: !bluetoothController.busy
                    onClicked: bluetoothController.togglePower()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Forget"
                    enabled: !bluetoothController.busy && deviceList.currentIndex >= 0
                        && bluetoothController.devices[deviceList.currentIndex].paired
                    onClicked: bluetoothController.forget(deviceList.currentIndex)
                }
            }

            ListView {
                id: deviceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 5
                model: bluetoothController.devices
                currentIndex: count > 0 ? 0 : -1
                highlightMoveDuration: 90
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: ListView.isCurrentItem
                    width: deviceList.width
                    height: 54
                    radius: 10
                    color: selected || mouse.containsMouse || modelData.connected ? root.primary : root.raised
                    border.color: selected || modelData.connected ? root.primary : root.outline

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        Label {
                            text: modelData.connected ? "●" : (modelData.paired ? "◆" : "◇")
                            color: selected || modelData.connected ? root.onPrimary : root.primary
                            font.pixelSize: 14
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                text: modelData.name
                                color: selected || modelData.connected ? root.onPrimary : root.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: modelData.connected ? "Connected · click to disconnect"
                                    : (modelData.paired ? "Paired · click to connect" : "Available · click to pair")
                                color: selected || modelData.connected ? root.onPrimary : root.muted
                                opacity: selected || modelData.connected ? 0.75 : 1
                                font.pixelSize: 10
                            }
                        }
                        Label {
                            text: modelData.address
                            color: selected || modelData.connected ? root.onPrimary : root.muted
                            opacity: 0.75
                            font.pixelSize: 9
                        }
                    }

                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !bluetoothController.busy
                        onEntered: deviceList.currentIndex = index
                        onClicked: bluetoothController.activate(index)
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: deviceList.count === 0 && !bluetoothController.busy
                    text: bluetoothController.powered ? "No devices found\nPress Scan to discover nearby devices" : "Bluetooth is turned off"
                    horizontalAlignment: Text.AlignHCenter
                    color: root.muted
                    lineHeight: 1.4
                }
            }
        }
    }
}
