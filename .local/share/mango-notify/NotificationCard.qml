import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    property double notificationId: 0
    property string sender: ""
    property string heading: ""
    property string message: ""
    property string iconSource: ""
    property string imageSource: ""
    property var createdAt
    property int urgency: 1
    property var notificationActions: []
    property int progress: -1
    property bool toastMode: false
    property bool expanded: false
    readonly property bool hasDefault: notificationActions.some(a => a.key === "default")
    readonly property bool hasReply: notificationActions.some(a => a.key === "inline-reply")
    readonly property string code: {
        const match = (heading + " " + message).match(/\b\d{6}\b/)
        return match ? match[0] : ""
    }
    signal dismissRequested()
    signal actionRequested(string key)
    signal replyRequested(string text)
    Accessible.role: toastMode ? Accessible.AlertMessage : Accessible.Grouping
    Accessible.name: sender + ": " + heading
    Accessible.description: message

    implicitHeight: content.implicitHeight + 28
    radius: 16
    color: themePalette.colors.surfaceContainer
    border.width: urgency === 2 ? 1 : 0
    border.color: urgency === 2 ? themePalette.colors.error : "transparent"

    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        spacing: 9
        RowLayout {
            spacing: 9
            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 9
                color: themePalette.colors.surfaceContainerHigh
                Text {
                    anchors.centerIn: parent
                    visible: appIcon.status !== Image.Ready
                    text: card.sender ? card.sender.charAt(0).toUpperCase() : "•"
                    color: themePalette.colors.onSurfaceVariant
                    font { family: "Roboto"; pixelSize: 13; weight: Font.DemiBold }
                }
                Image {
                    id: appIcon
                    anchors { fill: parent; margins: 4 }
                    source: card.iconSource.length === 0 ? "" : card.iconSource.startsWith("file:") || card.iconSource.startsWith("image:") ? card.iconSource : card.iconSource.startsWith("/") ? "file://" + card.iconSource : "image://desktop/" + encodeURIComponent(card.iconSource)
                    sourceSize: Qt.size(24, 24)
                    fillMode: Image.PreserveAspectFit
                }
            }
            Text {
                Layout.fillWidth: true
                text: card.sender || "Notification"
                textFormat: Text.PlainText
                color: themePalette.colors.onSurfaceVariant
                font { family: "Roboto"; pixelSize: 11; weight: Font.Medium }
                elide: Text.ElideRight
            }
            Text {
                text: card.createdAt ? Qt.formatDateTime(card.createdAt, "hh:mm") : ""
                color: themePalette.colors.onSurfaceVariant
                font { family: "Roboto"; pixelSize: 10 }
            }
            IconButton {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                symbol: "󰅖"
                description: "Dismiss notification"
                onClicked: card.dismissRequested()
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: card.heading
                textFormat: Text.PlainText
                color: themePalette.colors.onSurface
                font { family: "Roboto"; pixelSize: 14; weight: Font.DemiBold }
                wrapMode: Text.Wrap
                maximumLineCount: card.expanded ? 20 : 2
                elide: Text.ElideRight
            }
            Text {
                id: bodyText
                Layout.fillWidth: true
                visible: text.length > 0
                text: card.message
                textFormat: Text.PlainText
                color: themePalette.colors.onSurfaceVariant
                font { family: "Roboto"; pixelSize: 12 }
                wrapMode: Text.Wrap
                maximumLineCount: card.expanded ? 100 : card.toastMode ? 3 : 4
                elide: Text.ElideRight
            }
            TapHandler {
                enabled: card.hasDefault
                onTapped: card.actionRequested("default")
            }
        }
        Image {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.min(150, implicitHeight) : 0
            visible: card.imageSource.length > 0
            source: card.imageSource
            sourceSize: Qt.size(380, 150)
            fillMode: Image.PreserveAspectFit
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 5
            visible: card.progress >= 0
            color: themePalette.colors.surfaceContainerHigh
            radius: 3
            Rectangle {
                height: parent.height
                width: parent.width * Math.max(0, Math.min(100, card.progress)) / 100
                radius: 3
                color: themePalette.colors.primary
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: card.notificationActions.length > 0 || card.code.length > 0 || bodyText.truncated || card.expanded
            Repeater {
                model: card.notificationActions
                delegate: ActionButton {
                    required property var modelData
                    visible: modelData.key !== "inline-reply"
                    text: modelData.label || "Open"
                    onClicked: card.actionRequested(modelData.key)
                }
            }
            ActionButton {
                visible: card.code.length > 0
                text: "Copy " + card.code
                implicitHeight: 32
                onClicked: notifyControl.copyText(card.code)
            }
            ActionButton {
                visible: bodyText.truncated || card.expanded
                text: card.expanded ? "Show less" : "Show more"
                implicitHeight: 32
                onClicked: card.expanded = !card.expanded
            }
            ActionButton {
                visible: card.toastMode && card.hasReply
                text: "Reply"
                onClicked: notifyService.panelVisible = true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: card.hasReply && !card.toastMode
            TextField {
                id: replyInput
                Accessible.name: "Reply message"
                Layout.fillWidth: true
                placeholderText: "Reply"
                maximumLength: 4096
                color: themePalette.colors.onSurface
                placeholderTextColor: themePalette.colors.onSurfaceVariant
                background: Rectangle { color: themePalette.colors.surface; radius: 9; border.width: replyInput.activeFocus ? 1 : 0; border.color: themePalette.colors.primary }
                onAccepted: if (text.trim().length) { card.replyRequested(text); text = "" }
            }
            IconButton {
                symbol: "󰒊"
                description: "Send reply"
                enabled: replyInput.text.trim().length > 0
                onClicked: { card.replyRequested(replyInput.text); replyInput.text = "" }
            }
        }
    }
}
