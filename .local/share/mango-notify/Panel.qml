import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: panel
    property var colors: themePalette.colors

    component ConnectionTile: Rectangle {
        id: tile
        property string symbol
        property string label
        property string status
        property color accent
        property bool powered: false
        property bool available: true
        signal openRequested()
        signal powerRequested()
        implicitHeight: 118
        radius: 18
        color: Qt.tint(panel.colors.surfaceContainer, Qt.alpha(accent, 0.08))
        border.width: tile.activeFocus ? 2 : 0
        border.color: accent
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: label + ": " + status
        Keys.onReturnPressed: openRequested()
        MouseArea { anchors.fill: parent; onClicked: tile.openRequested() }
        ColumnLayout {
            anchors { fill: parent; margins: 15 }
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: tile.symbol
                    color: tile.powered ? tile.accent : panel.colors.onSurfaceVariant
                    font { family: "JetBrainsMono Nerd Font"; pixelSize: 26 }
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    symbol: "󰐥"
                    description: (tile.powered ? "Turn off " : "Turn on ") + tile.label
                    enabled: tile.available
                    accent: tile.accent
                    emphasized: tile.powered
                    implicitWidth: 28
                    implicitHeight: 28
                    onClicked: tile.powerRequested()
                }
            }
            Item { Layout.fillHeight: true }
            Text { text: tile.label; color: panel.colors.onSurface; font { family: "Roboto"; pixelSize: 13; weight: Font.DemiBold } }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: tile.status
                    textFormat: Text.PlainText
                    color: panel.colors.onSurfaceVariant
                    font { family: "Roboto"; pixelSize: 11 }
                    elide: Text.ElideRight
                }
                Text { text: "›"; color: panel.colors.onSurfaceVariant; font.pixelSize: 18 }
            }
        }
    }

    component NativeSlider: Slider {
        id: slider
        from: 0
        to: 100
        stepSize: 1
        implicitHeight: 34
        padding: 0
        background: Rectangle {
            x: slider.leftPadding
            y: (slider.height - height) / 2
            width: slider.availableWidth
            height: 6
            radius: 3
            color: Qt.alpha(panel.colors.onSurface, 0.12)
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 3; color: slider.enabled ? panel.colors.primary : panel.colors.onSurfaceVariant }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: (slider.height - height) / 2
            width: 14
            height: 14
            radius: 7
            color: slider.enabled ? panel.colors.onSurface : panel.colors.onSurfaceVariant
            border.width: slider.activeFocus ? 2 : 0
            border.color: panel.colors.primary
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: 20 }
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ConnectionTile {
                Layout.fillWidth: true
                symbol: "󰤨"
                label: "Network"
                status: !systemControls.wifiEnabled ? "Wi-Fi off" : systemControls.networkConnected ? systemControls.networkName : "Not connected"
                accent: panel.colors.blue
                powered: systemControls.wifiEnabled
                onOpenRequested: systemControls.launch("network")
                onPowerRequested: systemControls.setWifiEnabled(!systemControls.wifiEnabled)
            }
            ConnectionTile {
                Layout.fillWidth: true
                symbol: "󰂯"
                label: "Bluetooth"
                status: !systemControls.bluetoothAvailable ? "Unavailable" : systemControls.bluetoothEnabled ? "On" : "Off"
                accent: panel.colors.magenta
                powered: systemControls.bluetoothEnabled
                available: systemControls.bluetoothAvailable
                onOpenRequested: systemControls.launch("bluetooth")
                onPowerRequested: systemControls.setBluetoothEnabled(!systemControls.bluetoothEnabled)
            }
        }
        Item { Layout.preferredHeight: 18 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            IconButton {
                symbol: systemControls.muted ? "󰝟" : "󰕾"
                description: systemControls.muted ? "Unmute audio" : "Mute audio"
                emphasized: systemControls.muted
                enabled: systemControls.audioAvailable
                onClicked: systemControls.setMuted(!systemControls.muted)
            }
            NativeSlider {
                Layout.fillWidth: true
                value: systemControls.volume
                enabled: systemControls.audioAvailable
                Accessible.name: "Volume"
                onValueChanged: if (enabled && Math.round(value) !== systemControls.volume) systemControls.setVolume(Math.round(value))
            }
            Text {
                Layout.preferredWidth: 34
                text: systemControls.volume + "%"
                color: panel.colors.onSurfaceVariant
                font { family: "Roboto"; pixelSize: 11 }
                horizontalAlignment: Text.AlignRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            IconButton { symbol: "󰃠"; description: "Display brightness"; enabled: false }
            NativeSlider {
                Layout.fillWidth: true
                from: 5
                value: systemControls.brightness
                enabled: systemControls.brightnessAvailable
                Accessible.name: "Brightness"
                onValueChanged: if (enabled && Math.round(value) !== systemControls.brightness) systemControls.setBrightness(Math.round(value))
            }
            Text {
                Layout.preferredWidth: 34
                text: systemControls.brightnessAvailable ? systemControls.brightness + "%" : "—"
                color: panel.colors.onSurfaceVariant
                font { family: "Roboto"; pixelSize: 11 }
                horizontalAlignment: Text.AlignRight
            }
        }
        Item { Layout.preferredHeight: 8 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            IconButton {
                symbol: systemControls.micMuted ? "󰍭" : "󰍬"
                description: systemControls.micMuted ? "Unmute microphone" : "Mute microphone"
                emphasized: systemControls.micMuted
                enabled: systemControls.audioAvailable
                onClicked: systemControls.setMicMuted(!systemControls.micMuted)
            }
            Text { text: systemControls.micMuted ? "Microphone muted" : "Microphone on"; color: panel.colors.onSurfaceVariant; font { family: "Roboto"; pixelSize: 11 } }
            Item { Layout.fillWidth: true }
            IconButton { symbol: "󰒓"; description: "Audio settings"; onClicked: systemControls.launch("audio") }
        }
        Item { Layout.preferredHeight: 6 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.preferredWidth: 36
                text: "󰓅"
                color: panel.colors.primary
                font { family: "JetBrainsMono Nerd Font"; pixelSize: 19 }
                horizontalAlignment: Text.AlignHCenter
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Power profile"; color: panel.colors.onSurface; font { family: "Roboto"; pixelSize: 12; weight: Font.Medium } }
                Text {
                    Layout.fillWidth: true
                    text: systemControls.powerProfile || "Unavailable"
                    textFormat: Text.PlainText
                    color: panel.colors.onSurfaceVariant
                    font { family: "Roboto"; pixelSize: 10 }
                    elide: Text.ElideRight
                }
            }
            IconButton { symbol: "󰅂"; description: "Choose power profile"; onClicked: systemControls.launch("power") }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.preferredWidth: 36
                text: notifyService.dnd ? "󰂛" : "󰂚"
                color: notifyService.dnd ? panel.colors.primary : panel.colors.onSurfaceVariant
                font { family: "JetBrainsMono Nerd Font"; pixelSize: 19 }
                horizontalAlignment: Text.AlignHCenter
            }
            Text { Layout.fillWidth: true; text: "Do not disturb"; color: panel.colors.onSurface; font { family: "Roboto"; pixelSize: 12; weight: Font.Medium } }
            Switch {
                id: dndSwitch
                Accessible.name: "Do not disturb"
                checked: notifyService.dnd
                onToggled: notifyService.dnd = checked
                implicitWidth: 44
                implicitHeight: 36
                indicator: Rectangle {
                    x: 0; y: (dndSwitch.height - height) / 2
                    width: 40; height: 22; radius: 11
                    color: dndSwitch.checked ? panel.colors.primary : panel.colors.surfaceContainerHigh
                    border.width: dndSwitch.activeFocus ? 1 : 0
                    border.color: panel.colors.primary
                    Rectangle { x: dndSwitch.checked ? 21 : 3; y: 3; width: 16; height: 16; radius: 8; color: dndSwitch.checked ? panel.colors.onPrimary : panel.colors.onSurfaceVariant }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: systemControls.error.length > 0
            text: systemControls.error
            textFormat: Text.PlainText
            color: panel.colors.error
            font { family: "Roboto"; pixelSize: 11 }
            wrapMode: Text.Wrap
            maximumLineCount: 3
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 100 : 0
            Layout.topMargin: visible ? 12 : 0
            visible: systemControls.mediaAvailable
            color: panel.colors.surfaceContainer
            radius: 16
            RowLayout {
                anchors { fill: parent; margins: 12 }
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    radius: 12
                    color: panel.colors.surfaceContainerHigh
                    clip: true
                    Text {
                        anchors.centerIn: parent
                        text: "󰎆"
                        color: panel.colors.onSurfaceVariant
                        font { family: "JetBrainsMono Nerd Font"; pixelSize: 26 }
                        visible: albumArt.status !== Image.Ready
                    }
                    Image { id: albumArt; anchors.fill: parent; source: systemControls.mediaArt; sourceSize: Qt.size(128, 128); fillMode: Image.PreserveAspectCrop }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        text: systemControls.mediaTitle
                        textFormat: Text.PlainText
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                        color: panel.colors.onSurface
                        font { family: "Roboto"; pixelSize: 13; weight: Font.DemiBold }
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: systemControls.mediaArtist
                        textFormat: Text.PlainText
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                        color: panel.colors.onSurfaceVariant
                        font { family: "Roboto"; pixelSize: 11 }
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 4
                        IconButton { implicitHeight: 28; implicitWidth: 28; symbol: "󰒮"; description: "Previous track"; enabled: systemControls.canPrevious; onClicked: systemControls.mediaPrevious() }
                        IconButton { implicitHeight: 28; implicitWidth: 28; symbol: systemControls.mediaPlaying ? "󰏤" : "󰐊"; description: systemControls.mediaPlaying ? "Pause" : "Play"; onClicked: systemControls.mediaPlayPause() }
                        IconButton { implicitHeight: 28; implicitWidth: 28; symbol: "󰒭"; description: "Next track"; enabled: systemControls.canNext; onClicked: systemControls.mediaNext() }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 16; color: panel.colors.outlineVariant; opacity: 0.6 }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 10
            Text { text: "Notifications"; color: panel.colors.onSurface; font { family: "Roboto"; pixelSize: 16; weight: Font.DemiBold } }
            Text { text: notifyService.count; visible: notifyService.count > 0; color: panel.colors.onSurfaceVariant; font { family: "Roboto"; pixelSize: 11 } }
            Item { Layout.fillWidth: true }
            IconButton { text: "Clear"; description: "Clear notifications"; enabled: notifyService.count > 0; onClicked: notifyService.clear() }
            IconButton { symbol: "󰅖"; description: "Close panel"; onClicked: notifyService.panelVisible = false }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                visible: notifyService.count === 0
                text: "No notifications"
                color: panel.colors.onSurfaceVariant
                font { family: "Roboto"; pixelSize: 12 }
            }
            ListView {
                id: notifications
                anchors.fill: parent
                clip: true
                model: historyModel
                spacing: 8
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                section.property: "appName"
                section.criteria: ViewSection.FullString
                section.delegate: Text {
                    required property string section
                    width: notifications.width
                    height: 24
                    text: section
                    textFormat: Text.PlainText
                    color: panel.colors.onSurfaceVariant
                    font { family: "Roboto"; pixelSize: 10; weight: Font.Medium }
                    verticalAlignment: Text.AlignVCenter
                }
                delegate: NotificationCard {
                    required property var model
                    width: notifications.width
                    notificationId: model.notificationId
                    sender: model.appName
                    heading: model.summary
                    message: model.body
                    iconSource: model.icon
                    imageSource: model.image
                    createdAt: model.timestamp
                    urgency: model.urgency
                    notificationActions: model.actions
                    progress: model.progress
                    onDismissRequested: notifyService.dismiss(notificationId)
                    onActionRequested: function(key) { notifyService.invoke(notificationId, key) }
                    onReplyRequested: function(text) { notifyService.reply(notificationId, text) }
                }
            }
        }
    }
}
