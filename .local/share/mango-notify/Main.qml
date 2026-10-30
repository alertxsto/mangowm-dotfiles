import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: window
    property bool showingPanel: false
    property bool hadFocus: false
    readonly property int preferredHeight: showingPanel ? Math.max(320, Math.min(860, Screen.height - 76)) : Math.max(1, Math.min(Screen.height - 76, toastLoader.item ? toastLoader.item.implicitHeight : 1))
    width: 440
    height: preferredHeight
    minimumWidth: 440
    maximumWidth: 440
    x: Screen.virtualX + Screen.width - width - 12
    y: Screen.virtualY + 50
    color: "transparent"
    title: "Notifications"
    flags: Qt.Tool | Qt.FramelessWindowHint
    visible: showingPanel || notifyService.toastCount > 0

    onActiveChanged: {
        if (active)
            hadFocus = true
        else if (hadFocus && notifyService.panelVisible) {
            hadFocus = false
            notifyService.panelVisible = false
        }
    }
    onClosing: function(close) {
        close.accepted = false
        notifyService.panelVisible = false
        notifyService.hideToasts()
    }
    Connections {
        target: notifyService
        function onPanelVisibleChanged() {
            if (notifyService.panelVisible) {
                leave.stop()
                panelContent.x = 48
                panelContent.opacity = 0
                window.showingPanel = true
                enter.start()
                window.requestActivate()
                panelContent.forceActiveFocus()
            } else {
                enter.stop()
                leave.start()
            }
        }
    }
    ParallelAnimation {
        id: enter
        NumberAnimation { target: panelContent; property: "x"; to: 0; duration: 180; easing.type: Easing.OutCubic }
        NumberAnimation { target: panelContent; property: "opacity"; to: 1; duration: 140 }
    }
    ParallelAnimation {
        id: leave
        NumberAnimation { target: panelContent; property: "x"; to: 48; duration: 140; easing.type: Easing.InCubic }
        NumberAnimation { target: panelContent; property: "opacity"; to: 0; duration: 120 }
        onFinished: {
            if (!notifyService.panelVisible)
                window.showingPanel = false
        }
    }
    Item {
        anchors.fill: parent
        clip: true
        Item {
            id: panelContent
            width: window.width
            height: window.height
            focus: notifyService.panelVisible
            Keys.onEscapePressed: notifyService.panelVisible = false
            Rectangle {
                anchors.fill: parent
                visible: window.showingPanel
                color: themePalette.colors.surface
                radius: 24
                border.width: 1
                border.color: themePalette.colors.outlineVariant
            }
            Loader {
                anchors.fill: parent
                active: window.showingPanel
                source: "Panel.qml"
            }
        }
        Loader {
            id: toastLoader
            anchors.fill: parent
            active: !window.showingPanel && notifyService.toastCount > 0
            sourceComponent: Component {
                Item {
                    implicitHeight: list.contentHeight
                    ListView {
                        id: list
                        anchors.fill: parent
                        model: toastModel
                        spacing: 8
                        interactive: false
                        clip: true
                        delegate: NotificationCard {
                            required property var model
                            width: list.width
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
                            toastMode: true
                            onDismissRequested: notifyService.dismiss(notificationId)
                            onActionRequested: function(key) { notifyService.invoke(notificationId, key) }
                            onReplyRequested: function(text) { notifyService.reply(notificationId, text) }
                        }
                        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
                    }
                }
            }
        }
    }
}
