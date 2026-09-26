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
    readonly property string connectedDeviceName: {
        const devices = bluetoothController.devices
        for (let i = 0; i < devices.length; ++i) {
            if (devices[i].connected)
                return devices[i].name
        }
        return ""
    }
    readonly property int connectedCount: {
        const devices = bluetoothController.devices
        let count = 0
        for (let i = 0; i < devices.length; ++i) {
            if (devices[i].connected)
                ++count
        }
        return count
    }


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
        radius: 4
        color: root.surface
        border.color: root.outline
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 20
            anchors.bottomMargin: 18
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                Label {
                    text: "BLUETOOTH"
                    color: root.text
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: bluetoothController.powered ? "POWER ON" : "POWER OFF"
                    color: bluetoothController.powered ? root.primary : root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 15
                Layout.preferredHeight: 1
                color: root.primary
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 19
                spacing: 5
                Label {
                    text: root.connectedCount > 1 ? "CONNECTED DEVICES"
                        : root.connectedCount === 1 ? "CONNECTED DEVICE" : "DEVICE STATUS"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
                Label {
                    Layout.fillWidth: true
                    text: root.connectedCount > 1 ? root.connectedCount + " connected"
                        : root.connectedCount === 1 ? root.connectedDeviceName
                        : bluetoothController.busy ? "Updating Bluetooth"
                        : bluetoothController.powered ? "Ready to connect" : "Bluetooth is off"
                    color: root.text
                    font.pixelSize: 25
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: bluetoothController.busy ? bluetoothController.status
                        : root.connectedCount > 0 ? "Connected and ready to use"
                        : bluetoothController.powered ? "Choose a device or scan nearby"
                        : "Turn on to discover nearby devices"
                    color: root.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                Layout.preferredHeight: 36
                Layout.maximumHeight: 36
                spacing: 7

                Button {
                    id: powerButton
                    text: bluetoothController.powered ? "TURN OFF" : "TURN ON"
                    enabled: !bluetoothController.busy
                    Layout.preferredWidth: 99
                    Layout.preferredHeight: 36
                    onClicked: bluetoothController.togglePower()
                    contentItem: Label {
                        text: powerButton.text
                        color: powerButton.enabled ? (powerButton.down ? root.onPrimary : root.text) : root.muted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: powerButton.down && powerButton.enabled ? root.primary
                            : powerButton.hovered && powerButton.enabled ? root.raisedHigh : root.raised
                        border.color: root.outline
                        opacity: powerButton.enabled ? 1 : 0.6
                    }
                }
                Button {
                    id: scanButton
                    text: "SCAN"
                    enabled: !bluetoothController.busy
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: 36
                    onClicked: bluetoothController.scan()
                    contentItem: Label {
                        text: scanButton.text
                        color: scanButton.enabled ? (scanButton.down ? root.onPrimary : root.text) : root.muted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: scanButton.down && scanButton.enabled ? root.primary
                            : scanButton.hovered && scanButton.enabled ? root.raisedHigh : root.raised
                        border.color: root.outline
                        opacity: scanButton.enabled ? 1 : 0.6
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: forgetButton
                    text: "FORGET"
                    enabled: !bluetoothController.busy && deviceList.currentIndex >= 0
                        && bluetoothController.devices[deviceList.currentIndex].paired
                    Layout.preferredWidth: 83
                    Layout.preferredHeight: 36
                    onClicked: bluetoothController.forget(deviceList.currentIndex)
                    contentItem: Label {
                        text: forgetButton.text
                        color: forgetButton.enabled ? (forgetButton.down ? root.onPrimary : root.text) : root.muted
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        color: forgetButton.down && forgetButton.enabled ? root.primary
                            : forgetButton.hovered && forgetButton.enabled ? root.raisedHigh : root.raised
                        border.color: root.outline
                        opacity: forgetButton.enabled ? 1 : 0.6
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.bottomMargin: 9
                Label {
                    text: "DEVICES"
                    color: root.text
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.3
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: bluetoothController.busy ? "UPDATING" : deviceList.count + " FOUND"
                    color: root.muted
                    font.pixelSize: 10
                    font.letterSpacing: 0.7
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: root.outline
            }

            ListView {
                id: deviceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: bluetoothController.devices
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                onCountChanged: {
                    if (count === 0)
                        currentIndex = -1
                    else if (currentIndex < 0 || currentIndex >= count)
                        currentIndex = 0
                }

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: ListView.isCurrentItem
                    width: deviceList.width
                    height: 63
                    color: selected ? root.raisedHigh
                        : mouse.containsMouse ? root.raised : root.surface

                    Rectangle {
                        visible: selected
                        width: 2
                        height: parent.height - 1
                        color: root.primary
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        anchors.topMargin: 9
                        anchors.bottomMargin: 8
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: root.text
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Label {
                                text: modelData.connected ? "CONNECTED"
                                    : modelData.paired ? "PAIRED" : "AVAILABLE"
                                color: modelData.connected ? root.primary : root.muted
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.8
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: modelData.connected ? "Activate to disconnect"
                                    : modelData.paired ? "Activate to connect" : "Activate to pair"
                                color: root.muted
                                font.pixelSize: 10
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: modelData.address
                                color: root.muted
                                font.pixelSize: 9
                                font.letterSpacing: 0.3
                            }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.outline
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !bluetoothController.busy
                        onEntered: deviceList.currentIndex = index
                        onClicked: {
                            deviceList.currentIndex = index
                            bluetoothController.activate(index)
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    width: deviceList.width - 20
                    spacing: 8
                    visible: deviceList.count === 0
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: bluetoothController.busy ? "Updating devices"
                            : bluetoothController.powered ? "No devices found" : "Bluetooth is off"
                        color: root.text
                        font.pixelSize: 15
                        font.weight: Font.Medium
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: bluetoothController.busy ? bluetoothController.status
                            : bluetoothController.powered ? "Scan to discover nearby devices"
                            : "Turn on or scan to find devices"
                        color: root.muted
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: root.outline
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 12
                text: bluetoothController.status
                color: root.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
}
