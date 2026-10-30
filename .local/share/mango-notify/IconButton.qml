import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string symbol: ""
    property string description: ""
    property color accent: themePalette.colors.primary
    property bool emphasized: false
    implicitWidth: 36
    implicitHeight: 36
    padding: 6
    hoverEnabled: true
    Accessible.name: description || text
    ToolTip.visible: hovered && description.length > 0
    ToolTip.text: description
    ToolTip.delay: 500
    background: Rectangle {
        radius: height / 2
        color: control.emphasized ? control.accent : control.hovered || control.down ? themePalette.colors.surfaceContainerHigh : "transparent"
        border.width: control.activeFocus ? 2 : 0
        border.color: control.accent
    }
    contentItem: Text {
        text: control.symbol || control.text
        color: !control.enabled ? themePalette.colors.onSurfaceVariant : control.emphasized ? themePalette.colors.onPrimary : themePalette.colors.onSurface
        opacity: control.enabled ? 1 : 0.45
        font.family: control.symbol ? "JetBrainsMono Nerd Font" : "Roboto"
        font.pixelSize: control.symbol ? 18 : 12
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
