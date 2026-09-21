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
    title: "network-popup"

    property color primary: themeColors.primary
    property color onPrimary: themeColors.onPrimary
    property color surface: themeColors.surface
    property color raised: themeColors.surfaceContainer
    property color raisedHigh: themeColors.surfaceContainerHigh
    property color text: themeColors.onSurface
    property color muted: themeColors.onSurfaceVariant
    property color outline: themeColors.outlineVariant

    palette.window: surface
    palette.button: raisedHigh
    palette.buttonText: text
    palette.base: raised
    palette.text: text
    palette.highlight: primary
    palette.highlightedText: onPrimary
    property string passwordSsid: ""
    property bool everActive: false

    Component.onCompleted: activationTimer.start()
    onActiveChanged: {
        if (active) everActive = true
        else if (everActive && visible) closeTimer.restart()
    }
    Timer { id: activationTimer; interval: 180; onTriggered: root.requestActivate() }
    Timer { id: closeTimer; interval: 80; onTriggered: if (!root.active) Qt.quit() }

    Shortcut { sequence: "Esc"; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Up"
        enabled: root.passwordSsid === "" && networkList.count > 0
        onActivated: networkList.currentIndex = Math.max(0, networkList.currentIndex - 1)
    }
    Shortcut {
        sequence: "Down"
        enabled: root.passwordSsid === "" && networkList.count > 0
        onActivated: networkList.currentIndex = Math.min(networkList.count - 1, networkList.currentIndex + 1)
    }
    Shortcut {
        sequence: "Return"
        enabled: root.passwordSsid === "" && networkList.currentIndex >= 0
        onActivated: networkController.activate(networkList.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: root.passwordSsid === "" && networkList.currentIndex >= 0
        onActivated: networkController.activate(networkList.currentIndex)
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
                Label { text: "Wi-Fi"; color: root.primary; font.bold: true; font.pixelSize: 15 }
                Item { Layout.fillWidth: true }
                BusyIndicator { running: networkController.scanning; visible: running; Layout.preferredWidth: 25; Layout.preferredHeight: 25 }
                Label { text: networkController.scanning ? "Scanning…" : "Updated"; color: root.muted; font.pixelSize: 11 }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button { text: "Refresh"; onClicked: networkController.refresh() }
                Button { text: "Configure"; onClicked: networkController.advanced() }
                Item { Layout.fillWidth: true }
                Button { text: "Wi-Fi off"; onClicked: networkController.wifiOff() }
            }

            ListView {
                id: networkList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 5
                model: networkController.networks
                currentIndex: count > 0 ? 0 : -1
                highlightMoveDuration: 90
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: ListView.isCurrentItem
                    width: networkList.width
                    height: 48
                    radius: 10
                    color: selected || mouse.containsMouse || modelData.active ? root.primary : root.raised
                    border.color: selected || modelData.active ? root.primary : root.outline
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        Label {
                            text: modelData.signal >= 75 ? "▂▄▆█" : modelData.signal >= 50 ? "▂▄▆·" : modelData.signal >= 25 ? "▂▄··" : "▂···"
                            color: selected || modelData.active ? root.onPrimary : root.primary
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.ssid; color: selected || modelData.active ? root.onPrimary : root.text; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.active ? "Connected" : (modelData.secured ? "Secured" : "Open"); color: selected || modelData.active ? root.onPrimary : root.muted; opacity: selected || modelData.active ? 0.75 : 1; font.pixelSize: 10 }
                        }
                        Label { text: modelData.signal + "%"; color: selected || modelData.active ? root.onPrimary : root.muted }
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: networkList.currentIndex = index
                        onClicked: networkController.activate(index)
                    }
                }
            }
        }
    }

    Rectangle {
        visible: passwordSsid !== ""
        anchors.centerIn: parent
        width: parent.width - 40
        height: 150
        radius: 14
        color: root.raised
        border.color: root.primary
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 8
            Label { text: "Password · " + root.passwordSsid; color: root.text; elide: Text.ElideRight; Layout.fillWidth: true }
            TextField { id: password; echoMode: TextInput.Password; Layout.fillWidth: true; focus: visible; onAccepted: networkController.connectWithPassword(root.passwordSsid, text) }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: root.passwordSsid = "" }
                Button { text: "Connect"; onClicked: networkController.connectWithPassword(root.passwordSsid, password.text) }
            }
        }
    }

    Connections { target: networkController; function onPasswordRequested(ssid) { root.passwordSsid = ssid; password.forceActiveFocus() } }
}
