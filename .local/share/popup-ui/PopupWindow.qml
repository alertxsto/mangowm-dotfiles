import QtQuick
import QtQuick.Window

Window {
    id: window
    default property alias popupContent: body.data
    readonly property bool dismissing: exitAnimation.running
    property bool exitComplete: false

    function dismiss() {
        if (dismissing)
            return
        enterAnimation.stop()
        exitAnimation.start()
    }

    onClosing: function(close) {
        if (!exitComplete) {
            close.accepted = false
            dismiss()
        }
    }
    Component.onCompleted: enterAnimation.start()

    data: Item {
        anchors.fill: parent
        clip: true

        Item {
            id: body
            width: window.width
            height: window.height
            x: window.width + 8

            NumberAnimation {
                id: enterAnimation
                target: body
                property: "x"
                to: 0
                duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                id: exitAnimation
                target: body
                property: "x"
                to: window.width + 8
                duration: 140
                easing.type: Easing.InCubic
                onFinished: {
                    window.exitComplete = true
                    Qt.quit()
                }
            }
        }
    }
}
