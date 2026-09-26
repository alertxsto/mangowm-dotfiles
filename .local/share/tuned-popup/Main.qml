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
    title: "tuned-popup"

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
        enabled: profileList.count > 0
        onActivated: profileList.currentIndex = Math.max(0, profileList.currentIndex - 1)
    }
    Shortcut {
        sequence: "Down"
        enabled: profileList.count > 0
        onActivated: profileList.currentIndex = Math.min(profileList.count - 1, profileList.currentIndex + 1)
    }
    Shortcut {
        sequence: "Return"
        enabled: profileList.currentIndex >= 0 && !tunedController.busy
        onActivated: tunedController.activate(profileList.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: profileList.currentIndex >= 0 && !tunedController.busy
        onActivated: tunedController.activate(profileList.currentIndex)
    }

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: root.surface
        border.color: root.outline
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                spacing: 8

                Label {
                    text: "TUNED / POWER"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.4
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: profileList.count + (profileList.count === 1 ? " PROFILE" : " PROFILES")
                    color: root.muted
                    font.pixelSize: 10
                    font.letterSpacing: 0.8
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 9
                text: "Power profiles"
                color: root.text
                font.pixelSize: 21
                font.weight: Font.DemiBold
            }

            Label {
                Layout.topMargin: 22
                text: "CURRENT PROFILE"
                color: root.muted
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1.2
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 5
                text: tunedController.activeProfile || "No active profile"
                color: tunedController.activeProfile ? root.primary : root.text
                font.pixelSize: 23
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 18
                color: root.primary
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.bottomMargin: 9
                spacing: 8

                Label {
                    text: "AVAILABLE PROFILES"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: tunedController.busy
                    text: "WORKING"
                    color: root.primary
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
            }

            ListView {
                id: profileList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                opacity: tunedController.busy ? 0.65 : 1
                model: tunedController.profiles
                currentIndex: count > 0 ? 0 : -1
                highlightMoveDuration: 90
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool selected: ListView.isCurrentItem
                    readonly property bool isActive: modelData.name === tunedController.activeProfile

                    width: profileList.width
                    height: 64
                    radius: 2
                    color: selected ? root.raisedHigh : mouse.containsMouse ? root.raised : root.surface
                    border.width: selected ? 1 : 0
                    border.color: root.primary

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: root.text
                                font.pixelSize: 13
                                font.weight: isActive ? Font.DemiBold : Font.Medium
                                elide: Text.ElideRight
                            }
                            Label {
                                visible: isActive
                                text: "ACTIVE"
                                color: root.primary
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.7
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.description
                            color: root.muted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: root.outline
                        visible: !selected
                    }

                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !tunedController.busy
                        onEntered: profileList.currentIndex = index
                        onClicked: tunedController.activate(index)
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: profileList.contentHeight > profileList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    background: Item {}
                    contentItem: Rectangle {
                        implicitWidth: 3
                        radius: 1
                        color: root.outline
                    }
                }

                Column {
                    anchors.centerIn: parent
                    visible: profileList.count === 0
                    spacing: 8
                    width: parent.width - 24

                    Label {
                        width: parent.width
                        text: tunedController.busy || tunedController.status === "Updating…"
                              ? "Reading profiles" : "No profiles available"
                        color: root.text
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 15
                        font.weight: Font.Medium
                    }
                    Label {
                        width: parent.width
                        text: tunedController.busy || tunedController.status === "Updating…"
                              ? "Checking TuneD" : "Check that TuneD is running"
                        color: root.muted
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 9
                color: root.outline
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 47
                spacing: 10

                Label {
                    text: "STATUS"
                    color: root.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
                Label {
                    Layout.fillWidth: true
                    text: tunedController.status
                    color: tunedController.busy ? root.primary : root.text
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
            Label {
                Layout.fillWidth: true
                text: "UP / DOWN  SELECT     ENTER  ACTIVATE     ESC  CLOSE"
                color: root.muted
                font.pixelSize: 9
                font.letterSpacing: 0.3
            }
        }
    }
}
