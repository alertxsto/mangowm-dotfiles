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
    property bool initialScanPending: true
    readonly property bool isScanning: networkController.scanning || initialScanPending
    readonly property string connectedSsid: {
        const networks = networkController.networks
        for (let i = 0; i < networks.length; ++i) {
            if (networks[i].active)
                return networks[i].ssid
        }
        return ""
    }

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
        enabled: root.passwordSsid === "" && networkList.currentIndex >= 0 && networkList.currentIndex < networkList.count
        onActivated: networkController.activate(networkList.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: root.passwordSsid === "" && networkList.currentIndex >= 0 && networkList.currentIndex < networkList.count
        onActivated: networkController.activate(networkList.currentIndex)
    }

    Connections {
        target: networkController
        function onScanningChanged() {
            if (!networkController.scanning)
                root.initialScanPending = false
        }
        function onPasswordRequested(ssid) {
            root.passwordSsid = ssid
            password.clear()
            password.forceActiveFocus()
        }
    }

    component InstrumentButton: Button {
        id: control
        property bool emphasized: false
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 11
        font.weight: Font.DemiBold
        background: Rectangle {
            radius: 3
            color: control.emphasized ? root.primary
                                      : (control.hovered || control.activeFocus ? root.raisedHigh : root.raised)
            border.width: 1
            border.color: control.emphasized ? root.primary
                                            : (control.activeFocus ? root.primary : root.outline)
            opacity: control.enabled ? 1 : 0.48
        }
        contentItem: Text {
            text: control.text
            font: control.font
            color: control.emphasized ? root.onPrimary : root.text
            opacity: control.enabled ? 1 : 0.48
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: root.surface
        border.color: root.outline
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 19
            anchors.rightMargin: 19
            anchors.topMargin: 17
            anchors.bottomMargin: 13
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                Label {
                    text: "NETWORK  /  WI-FI"
                    color: root.text
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.isScanning ? "SCANNING" : "READY"
                    color: root.isScanning ? root.primary : root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
            }

            Item { Layout.preferredHeight: 22 }

            Label {
                Layout.fillWidth: true
                text: root.connectedSsid !== "" ? "CONNECTED TO" : "CONNECTION STATUS"
                color: root.muted
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: root.connectedSsid !== "" ? root.connectedSsid
                     : (root.isScanning ? "Finding networks" : "Not connected")
                color: root.text
                font.pixelSize: 25
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 5
                text: root.isScanning ? "Scanning nearby access points"
                     : (root.connectedSsid !== "" ? "Wi-Fi connection active" : "Select a network to connect")
                color: root.muted
                font.pixelSize: 11
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 19
                color: root.primary
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16
                spacing: 7
                InstrumentButton {
                    text: "Refresh"
                    enabled: !networkController.scanning
                    onClicked: networkController.refresh()
                }
                InstrumentButton {
                    text: "Advanced"
                    onClicked: networkController.advanced()
                }
                Item { Layout.fillWidth: true }
                InstrumentButton {
                    text: "Wi-Fi off"
                    onClicked: networkController.wifiOff()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 25
                Layout.bottomMargin: 9
                Label {
                    text: "AVAILABLE NETWORKS"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.isScanning ? "SEARCHING" : networkList.count + " FOUND"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.5
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.raised
                border.color: root.outline
                border.width: 1
                radius: 3
                clip: true

                ListView {
                    id: networkList
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: networkController.networks
                    currentIndex: -1
                    boundsBehavior: Flickable.StopAtBounds
                    onCountChanged: {
                        if (count === 0) currentIndex = -1
                        else if (currentIndex < 0 || currentIndex >= count) currentIndex = 0
                    }

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property bool selected: ListView.isCurrentItem
                        width: networkList.width
                        height: 56
                        color: selected || mouse.containsMouse ? root.raisedHigh : root.raised

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: root.outline
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 11
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.ssid
                                    elide: Text.ElideRight
                                    color: root.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: modelData.secured ? "SECURED" : "OPEN"
                                    color: root.muted
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 0.7
                                }
                            }
                            Label {
                                visible: modelData.active
                                text: "CONNECTED"
                                color: root.onPrimary
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.4
                                leftPadding: 6
                                rightPadding: 6
                                topPadding: 4
                                bottomPadding: 4
                                background: Rectangle { color: root.primary; radius: 2 }
                            }
                            Label {
                                text: modelData.signal + "%"
                                color: root.muted
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 31
                            }
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

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: networkList.count === 0
                    spacing: 7
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.isScanning ? "Scanning for networks" : "No networks found"
                        color: root.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.isScanning ? "Nearby networks will appear here"
                             : "Refresh to scan again or open Advanced"
                        color: root.muted
                        font.pixelSize: 11
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                Layout.topMargin: 5
                Label {
                    text: "UP / DOWN  SELECT     ENTER  OPEN"
                    color: root.muted
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.3
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "ESC  CLOSE"
                    color: root.muted
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.3
                }
            }
        }
    }

    Rectangle {
        visible: root.passwordSsid !== ""
        anchors.fill: parent
        radius: 4
        color: root.surface
        border.color: root.outline
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 19
            anchors.bottomMargin: 20
            spacing: 0
            Label {
                text: "NETWORK  /  CREDENTIALS"
                color: root.muted
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 16
                color: root.primary
            }
            Item { Layout.preferredHeight: 30 }
            Label {
                text: "Connect to"
                color: root.muted
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 3
                text: root.passwordSsid
                elide: Text.ElideRight
                color: root.text
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }
            Label {
                Layout.topMargin: 22
                text: "NETWORK PASSWORD"
                color: root.muted
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }
            TextField {
                id: password
                Layout.fillWidth: true
                Layout.preferredHeight: 43
                Layout.topMargin: 9
                leftPadding: 12
                rightPadding: 12
                placeholderText: "Enter password"
                echoMode: TextInput.Password
                color: root.text
                placeholderTextColor: root.muted
                selectionColor: root.primary
                selectedTextColor: root.onPrimary
                font.pixelSize: 13
                onAccepted: if (text.length > 0) networkController.connectWithPassword(root.passwordSsid, text)
                background: Rectangle {
                    color: root.raised
                    border.color: password.activeFocus ? root.primary : root.outline
                    border.width: 1
                    radius: 3
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                InstrumentButton {
                    text: "Cancel"
                    onClicked: {
                        password.clear()
                        root.passwordSsid = ""
                        networkList.forceActiveFocus()
                    }
                }
                InstrumentButton {
                    text: "Connect"
                    emphasized: true
                    enabled: password.text.length > 0
                    onClicked: networkController.connectWithPassword(root.passwordSsid, password.text)
                }
            }
        }
    }
}
