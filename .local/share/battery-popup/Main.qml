import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../popup-ui" as PopupUi

PopupUi.PopupWindow {
    id: root
    objectName: "batteryWindow"
    title: "battery-popup"
    width: Math.min(400, Screen.width - 24)
    // A stable Wayland surface keeps the close controls visible when details expand.
    height: Math.min(536, Screen.height - 70)
    minimumHeight: height
    maximumHeight: height
    x: Screen.virtualX + Screen.width - width - 12
    y: Screen.virtualY + 46
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    readonly property color surface: themeColors.surface
    readonly property color raised: themeColors.surfaceContainer
    readonly property color raisedHigh: themeColors.surfaceContainerHigh
    readonly property color foreground: themeColors.onSurface
    readonly property color muted: themeColors.onSurfaceVariant
    readonly property color outline: themeColors.outlineVariant
    readonly property color accent: themeColors.primary
    readonly property color warning: "#edc488"
    readonly property color critical: "#f2a69b"
    readonly property var numberLocale: Qt.locale("en_US")
    readonly property bool charging: batteryController.status === "Charging"
    readonly property bool discharging: batteryController.status === "Discharging"
    readonly property bool full: batteryController.status === "Full"
    readonly property bool low: discharging && batteryController.percentage >= 0 && batteryController.percentage <= 20
    readonly property bool isCritical: low && batteryController.percentage <= 10
    readonly property color statusColor: isCritical ? critical : low ? warning : accent
    property alias detailsExpanded: detailsButton.checked
    property bool everActive: false

    function numeric(value, decimals, unit) {
        return value >= 0 ? value.toLocaleString(numberLocale, 'f', decimals) + (unit || "") : "Unavailable"
    }

    function duration(seconds) {
        if (seconds < 60) return "< 1 min"
        const minutes = Math.max(1, Math.round(seconds / 60))
        const hours = Math.floor(minutes / 60)
        return hours > 0 ? hours + " h" + (minutes % 60 ? " " + minutes % 60 + " min" : "") : minutes + " min"
    }

    function statusText() {
        if (!batteryController.available) return "Battery unavailable"
        if (isCritical) return "Battery critical"
        if (low) return "Battery low"
        if (charging) return "Charging"
        if (full) return "Fully charged"
        if (discharging) return "On battery"
        if (batteryController.status === "Not charging") return "Not charging"
        return "Status unavailable"
    }

    Component.onCompleted: activationTimer.start()
    onActiveChanged: {
        if (active) {
            everActive = true
            dismissTimer.stop()
        } else if (everActive) {
            dismissTimer.restart()
        }
    }

    Timer {
        id: activationTimer
        interval: 180
        onTriggered: if (!root.everActive && !root.dismissing) root.requestActivate()
    }

    Timer {
        id: dismissTimer
        interval: 80
        onTriggered: if (!root.active) root.dismiss()
    }

    Shortcut { sequence: "Escape"; onActivated: root.dismiss() }

    NumberAnimation {
        id: detailScroll
        target: scroll.contentItem
        property: "contentY"
        duration: 160
        easing.type: Easing.OutCubic
        onFinished: detailsButton.revealPending = false
    }

    component Copy: Text {
        color: root.foreground
        font.family: "sans-serif"
        font.pixelSize: 12
        renderType: Text.NativeRendering
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.ignored: false
    }

    component Icon: Text {
        font.family: "JetBrainsMono Nerd Font Propo"
        font.pixelSize: 17
        color: root.accent
        renderType: Text.NativeRendering
        Accessible.ignored: true
    }

    component Metric: Rectangle {
        id: metric
        required property string iconText
        required property string label
        required property string value
        required property string caption
        implicitHeight: metricContent.implicitHeight + 28
        color: root.raised
        border.color: root.outline
        radius: 11

        ColumnLayout {
            id: metricContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Icon { text: metric.iconText; font.pixelSize: 14; color: root.muted }
                Copy {
                    Layout.fillWidth: true
                    text: metric.label
                    color: root.muted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
            Copy {
                Layout.fillWidth: true
                text: metric.value
                font.pixelSize: 29
                font.letterSpacing: -0.7
                elide: Text.ElideRight
            }
            Copy {
                Layout.fillWidth: true
                text: metric.caption
                color: root.muted
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
        }
    }

    component Detail: RowLayout {
        id: detail
        required property string label
        required property string value
        spacing: 14
        Layout.fillWidth: true
        Copy {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: detail.label
            font.pixelSize: 11
            color: root.muted
            wrapMode: Text.Wrap
        }
        Copy {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: detail.value
            font.pixelSize: 11
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surface
        radius: 16
        border.width: 1
        border.color: root.outline
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 63

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 18
            spacing: 9
            Icon { text: "󰁹"; font.pixelSize: 18 }
            Copy { text: "Battery"; font.pixelSize: 15; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: batteryController.available
                implicitWidth: deviceText.implicitWidth + 14
                implicitHeight: 24
                color: "transparent"
                border.color: root.outline
                radius: 5
                Copy {
                    id: deviceText
                    anchors.centerIn: parent
                    text: batteryController.deviceName
                    color: root.muted
                    font.pixelSize: 10
                }
            }
            ToolButton {
                id: closeButton
                objectName: "closeButton"
                Accessible.name: "Close battery popup"
                implicitWidth: 28
                implicitHeight: 28
                hoverEnabled: true
                onClicked: root.dismiss()
                contentItem: Copy {
                    text: "×"
                    font.pixelSize: 23
                    color: closeButton.hovered ? root.foreground : root.muted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: closeButton.hovered ? root.raised : "transparent"
                    border.width: closeButton.activeFocus ? 1 : 0
                    border.color: root.accent
                }
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        function revealDetails() {
            if (!detailsButton.revealPending)
                return
            detailScroll.to = Math.max(0, Math.min(detailsButton.y - 8, contentHeight - height))
            detailScroll.restart()
        }
        onContentHeightChanged: revealDetails()

        ColumnLayout {
            id: body
            width: scroll.availableWidth
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: 9
                Row {
                    spacing: 3
                    Copy {
                        objectName: "percentageValue"
                        text: batteryController.percentage >= 0 ? batteryController.percentage : "—"
                        font.pixelSize: 68
                        font.letterSpacing: -3
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 74
                    }
                    Copy {
                        text: "%"
                        visible: batteryController.percentage >= 0
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 9
                        font.pixelSize: 27
                        color: root.muted
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Copy {
                        objectName: "statusValue"
                        Layout.fillWidth: true
                        text: root.statusText()
                        horizontalAlignment: Text.AlignRight
                        color: root.statusColor
                        font.pixelSize: 12
                    }
                    Copy {
                        objectName: "estimateValue"
                        Layout.fillWidth: true
                        text: root.full ? "Charging complete" : batteryController.estimateSeconds >= 0
                              ? (batteryController.approximateEstimate ? "≈ " : "") + root.duration(batteryController.estimateSeconds)
                                + (root.charging ? " until full" : " remaining") : "Estimate unavailable"
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Copy {
                        Layout.fillWidth: true
                        text: batteryController.estimateSeconds >= 0 ? "At the current rate" : ""
                        visible: text.length > 0
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 10
                        color: root.muted
                        wrapMode: Text.Wrap
                    }
                }
            }

            Item { Layout.preferredHeight: 16 }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                Rectangle { anchors.fill: parent; radius: 3; color: root.raisedHigh }
                Rectangle {
                    height: parent.height
                    width: parent.width * Math.min(100, Math.max(0, batteryController.percentage)) / 100
                    radius: 3
                    color: root.statusColor
                }
                Row {
                    anchors.fill: parent
                    Repeater {
                        model: 10
                        Item {
                            required property int index
                            width: parent.width / 10
                            height: parent.height
                            Rectangle {
                                anchors.right: parent.right
                                height: parent.height
                                width: 2
                                color: root.surface
                                visible: index < 9
                            }
                        }
                    }
                }
                Accessible.role: Accessible.ProgressBar
                Accessible.name: "Battery charge"
                Accessible.description: batteryController.percentage >= 0 ? batteryController.percentage + " percent" : "Unavailable"
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 9
                spacing: 12
                Copy {
                    Layout.fillWidth: true
                    text: root.charging ? "Power into battery" : root.discharging ? "Battery power draw" : "Battery power"
                    font.pixelSize: 11
                    color: root.muted
                }
                Copy { text: root.numeric(batteryController.power, 2, " W"); font.pixelSize: 11 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: visible ? 15 : 0
                implicitHeight: warningText.implicitHeight + 20
                visible: root.low
                radius: 8
                color: root.raised
                border.color: root.statusColor
                Copy {
                    id: warningText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.isCritical ? "Connect a charger. Battery level is at or below 10%." : "Battery level is at or below 20%. Keep a charger nearby."
                    color: root.statusColor
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 23
                spacing: 10
                Metric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    iconText: "󰣐"
                    label: "Battery health"
                    value: batteryController.health >= 0 ? root.numeric(batteryController.health, 1, "%") : "—"
                    caption: batteryController.health >= 0 ? "Of design capacity" : "Capacity unavailable"
                }
                Metric {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    iconText: "󰑖"
                    label: "Charge cycles"
                    value: batteryController.cycleCount >= 0 ? root.numeric(batteryController.cycleCount, 0, "") : "—"
                    caption: batteryController.cycleCount >= 0 ? "Reported by firmware" : "Not reported by battery"
                }
            }

            Copy {
                Layout.fillWidth: true
                Layout.topMargin: 11
                text: batteryController.health > 100
                      ? "Reported full capacity exceeds design capacity. This ratio is not a cell-health diagnosis."
                      : batteryController.health >= 0
                        ? "Full-charge capacity relative to design capacity; not a cell-health diagnosis."
                        : "Health cannot be calculated without full-charge and design capacities."
                color: root.muted
                font.pixelSize: 10
                wrapMode: Text.Wrap
                lineHeight: 1.35
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.bottomMargin: 12
                spacing: 10
                Copy {
                    Layout.fillWidth: true
                    text: "Stored energy / full capacity"
                    color: root.muted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
                Copy {
                    text: batteryController.energy >= 0 && batteryController.fullEnergy >= 0
                          ? root.numeric(batteryController.energy, 2, "") + " / " + root.numeric(batteryController.fullEnergy, 2, " Wh") : "Unavailable"
                    font.pixelSize: 10
                }
            }

            ToolButton {
                id: detailsButton
                objectName: "detailsButton"
                property bool revealPending: false
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                implicitHeight: 46
                padding: 12
                hoverEnabled: true
                checkable: true
                Accessible.name: "Battery details"
                Accessible.description: root.detailsExpanded ? "Expanded" : "Collapsed"
                onToggled: {
                    detailScroll.stop()
                    revealPending = checked
                }
                contentItem: RowLayout {
                    Copy {
                        Layout.fillWidth: true
                        text: "Battery details"
                        font.pixelSize: 12
                        Accessible.ignored: true
                    }
                    Icon {
                        text: root.detailsExpanded ? "󰅃" : "󰅀"
                        font.pixelSize: 17
                        color: root.muted
                    }
                }
                background: Rectangle {
                    color: detailsButton.checked || detailsButton.hovered ? root.raisedHigh : root.raised
                    radius: 9
                    border.width: 1
                    border.color: detailsButton.activeFocus ? root.accent : root.outline
                }
            }

            Loader {
                Layout.fillWidth: true
                Layout.bottomMargin: active ? 12 : 0
                active: root.detailsExpanded
                visible: active
                onLoaded: {
                    detailsButton.revealPending = true
                    Qt.callLater(scroll.revealDetails)
                }
                sourceComponent: Rectangle {
                    color: root.raised
                    radius: 10
                    border.color: root.outline
                    implicitHeight: detailRows.implicitHeight + 28
                    Accessible.role: Accessible.Grouping
                    Accessible.name: "Battery details panel"

                    ColumnLayout {
                        id: detailRows
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 11

                        Detail { label: "Design capacity"; value: root.numeric(batteryController.designEnergy, 2, " Wh") }
                        Detail { label: "Full-charge capacity"; value: root.numeric(batteryController.fullEnergy, 2, " Wh") }
                        Detail { label: "Voltage"; value: root.numeric(batteryController.voltage, 2, " V") }
                        Detail {
                            label: "Temperature"
                            value: batteryController.temperature > -273.15 ? batteryController.temperature.toLocaleString(root.numberLocale, 'f', 1) + " °C" : "Unavailable"
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.outline }
                        Detail { label: "Manufacturer"; value: batteryController.manufacturer || "Unavailable" }
                        Detail { label: "Model"; value: batteryController.model || "Unavailable" }
                        Detail { label: "Technology"; value: batteryController.technology || "Unavailable" }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.outline }
                        Detail {
                            label: "Charge thresholds"
                            value: batteryController.chargeEnd >= 0
                                   ? (batteryController.chargeStart >= 0 ? root.numeric(batteryController.chargeStart, 0, "%") + " → " : "Up to ") + root.numeric(batteryController.chargeEnd, 0, "%")
                                   : "Unavailable"
                        }
                        Copy {
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            text: "Read-only. Temperature and charge thresholds are shown only when reported by the device."
                            color: root.muted
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            lineHeight: 1.35
                        }
                    }
                }
            }
        }
    }

    Item {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 43
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            color: root.raised
            bottomLeftRadius: 15
            bottomRightRadius: 15
        }
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: root.outline }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 7
            Icon {
                text: batteryController.adapterOnline === 1 ? "󰚥" : "󰁹"
                font.pixelSize: 13
                color: root.muted
            }
            Copy {
                Layout.fillWidth: true
                text: batteryController.adapterOnline === 1 ? "AC connected"
                      : batteryController.adapterOnline === 0 ? "AC disconnected" : "AC status unavailable"
                color: root.muted
                font.pixelSize: 10
            }
            Copy { text: "Esc"; color: root.muted; font.pixelSize: 10; font.weight: Font.Medium }
            Copy { text: "close"; color: root.muted; font.pixelSize: 10 }
        }
    }
}
