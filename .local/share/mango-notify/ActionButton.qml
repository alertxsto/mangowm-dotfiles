import QtQuick
import QtQuick.Controls

Button {
    id: control
    implicitHeight: 32
    padding: 8
    leftPadding: 11
    rightPadding: 11
    hoverEnabled: true
    background: Rectangle {
        radius: 9
        color: control.hovered || control.down ? themePalette.colors.surfaceContainerHigh : Qt.alpha(themePalette.colors.primary, 0.13)
        border.width: control.activeFocus ? 1 : 0
        border.color: themePalette.colors.primary
    }
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        color: themePalette.colors.onSurface
        font { family: "Roboto"; pixelSize: 11; weight: Font.Medium }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
