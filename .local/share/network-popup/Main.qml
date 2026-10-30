import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../popup-ui" as PopupUi

PopupUi.PopupWindow {
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
    property string passwordUuid: ""
    property string detailsSsid: ""
    property string detailsUuid: ""
    property string detailsSecurity: ""
    property bool confirmForget: false
    readonly property int detailsIndex: {
        const networks = networkController.networks
        for (let i = 0; i < networks.length; ++i) {
            if (detailsUuid !== "" ? networks[i].uuid === detailsUuid
                                   : !networks[i].uuid && networks[i].ssid === detailsSsid
                                     && networks[i].security === detailsSecurity)
                return i
        }
        return -1
    }
    readonly property var detailsNetwork: detailsIndex >= 0
                                          ? networkController.networks[detailsIndex] : null
    function showDetails(network) {
        detailsSsid = network.ssid
        detailsUuid = network.uuid || ""
        detailsSecurity = network.security || ""
        confirmForget = false
    }
    function activateSelected() {
        const index = networkList.currentIndex
        const network = networkController.networks[index]
        if (!network) return
        if (!network.inRange && !network.active) showDetails(network)
        else networkController.activate(index)
    }
    property bool everActive: false
    property bool initialScanPending: true
    readonly property bool isScanning: networkController.scanning || initialScanPending
    readonly property string connectedSsid: networkController.activeSsid

    Component.onCompleted: activationTimer.start()
    onActiveChanged: {
        if (active) everActive = true
        else if (everActive && visible) closeTimer.restart()
    }
    Timer { id: activationTimer; interval: 180; onTriggered: if (!root.everActive && !root.dismissing) root.requestActivate() }
    Timer { id: closeTimer; interval: 80; onTriggered: if (!root.active) root.dismiss() }

    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (root.confirmForget) root.confirmForget = false
            else if (root.detailsSsid !== "") root.detailsSsid = ""
            else if (root.passwordSsid !== "") {
                password.clear()
                root.passwordSsid = ""
            } else root.dismiss()
        }
    }
    Shortcut {
        sequence: "Up"
        enabled: root.passwordSsid === "" && root.detailsSsid === "" && networkList.count > 0
        onActivated: networkList.currentIndex = Math.max(0, networkList.currentIndex - 1)
    }
    Shortcut {
        sequence: "Down"
        enabled: root.passwordSsid === "" && root.detailsSsid === "" && networkList.count > 0
        onActivated: networkList.currentIndex = Math.min(networkList.count - 1, networkList.currentIndex + 1)
    }
    Shortcut {
        sequence: "Return"
        enabled: root.passwordSsid === "" && root.detailsSsid === "" && !networkController.busy && networkList.currentIndex >= 0 && networkList.currentIndex < networkList.count
        onActivated: root.activateSelected()
    }
    Shortcut {
        sequence: "Enter"
        enabled: root.passwordSsid === "" && root.detailsSsid === "" && !networkController.busy && networkList.currentIndex >= 0 && networkList.currentIndex < networkList.count
        onActivated: root.activateSelected()
    }

    Connections {
        target: networkController
        function onDismissRequested() { root.dismiss() }
        function onScanningChanged() {
            if (!networkController.scanning)
                root.initialScanPending = false
        }
        function onPasswordRequested(ssid, uuid) {
            root.passwordSsid = ssid
            root.passwordUuid = uuid
            password.clear()
            password.forceActiveFocus()
        }
        function onConnectionSucceeded() {
            password.clear()
            root.passwordSsid = ""
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
                    text: root.isScanning ? "SCANNING" : (networkController.busy ? "WORKING"
                          : (!networkController.radioKnown ? "CHECKING"
                          : (networkController.wifiEnabled ? "READY" : "OFFLINE")))
                    color: root.isScanning || networkController.busy ? root.primary : root.muted
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
                     : (!networkController.radioKnown ? "Checking Wi-Fi"
                     : (!networkController.wifiEnabled ? "Wi-Fi is off"
                     : (networkController.connected ? "Connected to Wi-Fi"
                     : (root.isScanning ? "Finding networks" : "Not connected"))))
                color: root.text
                font.pixelSize: 25
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 5
                text: networkController.errorMessage !== "" ? networkController.errorMessage
                     : (networkController.message !== "" ? networkController.message
                     : (!networkController.radioKnown ? "Reading NetworkManager state"
                     : (!networkController.wifiEnabled ? "Turn on Wi-Fi to see nearby networks"
                     : (root.isScanning ? "Scanning nearby access points"
                     : (networkController.connected ? "Wi-Fi connection active"
                     : "Select a network to connect")))))
                color: networkController.errorMessage !== "" ? root.primary : root.muted
                elide: Text.ElideRight
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
                    enabled: !networkController.busy && networkController.wifiEnabled && !networkController.scanning
                    onClicked: networkController.refresh()
                }
                InstrumentButton {
                    text: "Advanced"
                    onClicked: networkController.advanced()
                }
                Item { Layout.fillWidth: true }
                InstrumentButton {
                    text: networkController.wifiEnabled ? "Wi-Fi off" : "Wi-Fi on"
                    enabled: networkController.radioKnown && !networkController.busy
                    onClicked: networkController.setWifiEnabled(!networkController.wifiEnabled)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 25
                Layout.bottomMargin: 9
                Label {
                    text: "NETWORKS  /  SAVED & AVAILABLE"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.isScanning ? "SEARCHING" : networkList.count + " SHOWN"
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
                            z: 1
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 11
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.saved
                                          ? modelData.ssid + "  /  " + (modelData.name || "Saved") + "  /  " + modelData.uuid
                                          : modelData.ssid
                                    elide: Text.ElideRight
                                    color: root.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: (modelData.saved ? "SAVED  /  " : "")
                                          + (modelData.inRange ? (modelData.secured ? "SECURED" : "OPEN") : "OUT OF RANGE")
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
                                text: modelData.inRange ? modelData.signal + "%" : "—"
                                color: root.muted
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 31
                            }
                            InstrumentButton {
                                text: "Details"
                                implicitHeight: 30
                                leftPadding: 8
                                rightPadding: 8
                                onClicked: root.showDetails(modelData)
                            }
                        }
                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: networkList.currentIndex = index
                            onClicked: {
                                networkList.currentIndex = index
                                if (!networkController.busy && modelData.inRange)
                                    networkController.activate(index)
                                else if (!modelData.inRange)
                                    root.showDetails(modelData)
                            }
                        }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: networkList.count === 0
                    spacing: 7
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: !networkController.radioKnown ? "Checking Wi-Fi"
                              : (!networkController.wifiEnabled ? "Wi-Fi is off"
                              : (root.isScanning ? "Scanning for networks" : "No networks found"))
                        color: root.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: !networkController.wifiEnabled ? "Turn on Wi-Fi to find networks"
                              : (root.isScanning ? "Nearby networks will appear here"
                              : "Refresh to scan again or open Advanced")
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
        visible: root.detailsSsid !== "" && root.passwordSsid === ""
        anchors.fill: parent
        radius: 4
        color: root.surface
        border.color: root.outline
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 0
            Label {
                text: root.confirmForget ? "NETWORK  /  CONFIRM" : "NETWORK  /  DETAILS"
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
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 24
                text: root.detailsSsid
                elide: Text.ElideRight
                color: root.text
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }
            ColumnLayout {
                visible: !root.confirmForget && root.detailsNetwork !== null
                Layout.fillWidth: true
                Layout.topMargin: 22
                spacing: 12
                Repeater {
                    model: [
                        { name: "SIGNAL", value: root.detailsNetwork && root.detailsNetwork.inRange
                                                  ? root.detailsNetwork.signal + "%" : "Out of range" },
                        { name: "SECURITY", value: root.detailsNetwork ? (root.detailsNetwork.security || "Unknown") : "Unknown" },
                        { name: "PROFILE", value: root.detailsNetwork && root.detailsNetwork.saved
                                                  ? (root.detailsNetwork.name || "Saved") + " / " + root.detailsNetwork.uuid : "Not saved" },
                        { name: "IP ADDRESS", value: root.detailsNetwork ? (root.detailsNetwork.ipAddress || "Not available") : "Not available" },
                        { name: "INTERFACE", value: root.detailsNetwork ? (root.detailsNetwork.interface || "Not available") : "Not available" }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            text: modelData.name
                            color: root.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.8
                            Layout.preferredWidth: 100
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: root.text
                            font.pixelSize: 12
                            elide: modelData.name === "PROFILE" ? Text.ElideNone : Text.ElideRight
                            wrapMode: modelData.name === "PROFILE" ? Text.WrapAnywhere : Text.NoWrap
                        }
                    }
                }
                Switch {
                    visible: root.detailsNetwork && root.detailsNetwork.saved
                    enabled: !networkController.busy && root.detailsNetwork !== null
                    text: "Connect automatically"
                    checked: root.detailsNetwork ? root.detailsNetwork.autoconnect : false
                    onClicked: networkController.setAutoconnect(root.detailsUuid, checked)
                }
            }
            Label {
                visible: root.confirmForget
                Layout.fillWidth: true
                Layout.topMargin: 24
                text: root.detailsNetwork && root.detailsNetwork.active
                      ? "Forget this network? It will disconnect and its saved settings will be removed."
                      : "Forget this saved network? Its saved connection settings will be removed."
                wrapMode: Text.Wrap
                color: root.text
                font.pixelSize: 13
            }
            Label {
                visible: !root.confirmForget && root.detailsNetwork === null
                Layout.fillWidth: true
                Layout.topMargin: 24
                text: "This network is no longer available."
                color: root.muted
                font.pixelSize: 12
            }
            Label {
                visible: networkController.errorMessage !== ""
                Layout.fillWidth: true
                Layout.topMargin: 12
                text: networkController.errorMessage
                color: root.primary
                wrapMode: Text.Wrap
                font.pixelSize: 11
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                InstrumentButton {
                    visible: !root.confirmForget && root.detailsNetwork && root.detailsNetwork.saved
                    text: "Forget"
                    enabled: !networkController.busy
                    onClicked: root.confirmForget = true
                }
                Item { Layout.fillWidth: true }
                InstrumentButton {
                    text: root.confirmForget ? "Cancel" : "Back"
                    onClicked: {
                        if (root.confirmForget) root.confirmForget = false
                        else root.detailsSsid = ""
                    }
                }
                InstrumentButton {
                    visible: root.confirmForget
                    text: "Forget network"
                    emphasized: true
                    enabled: !networkController.busy && root.detailsNetwork !== null
                    onClicked: {
                        networkController.forget(root.detailsUuid)
                        root.confirmForget = false
                        root.detailsSsid = ""
                    }
                }
                InstrumentButton {
                    visible: !root.confirmForget && root.detailsNetwork !== null
                    text: root.detailsNetwork && root.detailsNetwork.active ? "Disconnect" : "Connect"
                    emphasized: true
                    enabled: !networkController.busy && networkController.wifiEnabled
                             && root.detailsNetwork !== null
                             && (root.detailsNetwork.inRange || root.detailsNetwork.active)
                    onClicked: {
                        const index = root.detailsIndex
                        root.detailsSsid = ""
                        networkController.activate(index)
                    }
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
                onAccepted: if (text.length > 0 && !networkController.busy)
                                networkController.connectWithPassword(root.passwordSsid, root.passwordUuid, text)
                background: Rectangle {
                    color: root.raised
                    border.color: password.activeFocus ? root.primary : root.outline
                    border.width: 1
                    radius: 3
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 12
                visible: networkController.errorMessage !== "" || networkController.busy
                text: networkController.errorMessage !== ""
                      ? networkController.errorMessage : networkController.message
                color: networkController.errorMessage !== "" ? root.primary : root.muted
                wrapMode: Text.Wrap
                font.pixelSize: 11
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
                    enabled: password.text.length > 0 && !networkController.busy
                    onClicked: networkController.connectWithPassword(root.passwordSsid, root.passwordUuid, password.text)
                }
            }
        }
    }
}
