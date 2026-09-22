import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 1180
    height: 560
    x: Math.round((Screen.width - width) / 2)
    y: Math.max(52, Math.round((Screen.height - height) / 2))
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    title: "Mango Wallpaper Gallery"

    property var colors: themeColors
    property string selectedPath: ""
    property string activeCategory: "All"
    property var visibleEntries: []
    property int matchCount: 0
    readonly property real selectedCardWidth: 570
    readonly property real selectedCardHeight: 321
    readonly property real cardSeparation: 185
    property bool everActive: false

    function entryAt(index) {
        return index >= 0 && index < visibleEntries.length ? visibleEntries[index] : null
    }

    function entryMatches(entry) {
        if (!entry)
            return false
        const categoryMatch = activeCategory === "All" || entry.category === activeCategory
        const needle = searchField.text.trim().toLowerCase()
        return categoryMatch && (needle.length === 0 || entry.search.indexOf(needle) !== -1)
    }

    function rebuildEntries(preferredPath) {
        const current = entryAt(carousel.currentIndex)
        const keepPath = preferredPath || (current ? current.path : "")
        const filtered = []
        for (let index = 0; index < wallpaperEntries.length; ++index) {
            if (entryMatches(wallpaperEntries[index]))
                filtered.push(wallpaperEntries[index])
        }

        visibleEntries = filtered
        matchCount = filtered.length
        if (filtered.length === 0) {
            carousel.currentIndex = -1
            return
        }

        let nextIndex = 0
        if (keepPath.length > 0) {
            for (let index = 0; index < filtered.length; ++index) {
                if (filtered[index].path === keepPath) {
                    nextIndex = index
                    break
                }
            }
        }
        carousel.currentIndex = nextIndex
        Qt.callLater(function() {
            carousel.positionViewAtIndex(nextIndex, ListView.Center)
        })
    }

    function selectIndex(index) {
        if (index >= 0 && index < visibleEntries.length)
            carousel.currentIndex = index
    }

    function moveSelection(direction) {
        if (visibleEntries.length === 0)
            return
        const index = (carousel.currentIndex + direction + visibleEntries.length)
                    % visibleEntries.length
        selectIndex(index)
    }

    function resetSelection() {
        rebuildEntries("")
    }

    function applyCurrent() {
        const entry = entryAt(carousel.currentIndex)
        if (entry) {
            selectedPath = entry.path
            Qt.quit()
        }
    }

    Component.onCompleted: {
        const initial = initialWallpaperIndex >= 0
                      && initialWallpaperIndex < wallpaperEntries.length
                      ? wallpaperEntries[initialWallpaperIndex].path : ""
        rebuildEntries(initial)
        activationTimer.start()
    }
    onActiveChanged: {
        if (active)
            everActive = true
        else if (everActive)
            closeTimer.restart()
    }

    Timer { id: activationTimer; interval: 180; onTriggered: root.requestActivate() }
    Timer { id: closeTimer; interval: 220; onTriggered: if (!root.active) Qt.quit() }

    Item {
        id: keyboard
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Left) {
                root.moveSelection(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                root.moveSelection(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                categoryRow.selectAdjacent(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Tab) {
                categoryRow.selectAdjacent(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.applyCurrent()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                if (searchField.text.length > 0) {
                    searchField.text = ""
                    root.resetSelection()
                } else {
                    Qt.quit()
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                searchField.text = searchField.text.slice(0, -1)
                root.resetSelection()
                event.accepted = true
            } else if (event.text && event.text.length === 1
                       && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
                searchField.text += event.text
                root.resetSelection()
                event.accepted = true
            }
        }
    }

    Rectangle {
        id: topBar
        x: 18
        y: 10
        width: parent.width - 36
        height: 62
        radius: 20
        color: Qt.rgba(colors.surface.r, colors.surface.g, colors.surface.b, 0.96)
        border.width: 1
        border.color: colors.outlineVariant

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Text {
                text: "󰸉"
                color: colors.primary
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 22
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: "WALLPAPER"
                    color: colors.onSurface
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Text {
                    text: "← → browse  ·  Enter apply  ·  Esc close"
                    color: colors.onSurfaceVariant
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 9
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 310
            height: 40
            radius: 13
            color: colors.surfaceContainer
            border.width: 1
            border.color: searchField.text.length > 0 ? colors.primary : colors.outlineVariant

            Text {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                text: "󰍉"
                color: colors.primary
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 16
            }
            TextInput {
                id: searchField
                x: 42
                width: parent.width - 56
                anchors.verticalCenter: parent.verticalCenter
                readOnly: true
                color: colors.onSurface
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 12
            }
            Text {
                x: 42
                anchors.verticalCenter: parent.verticalCenter
                visible: searchField.text.length === 0
                text: "Type to filter"
                color: colors.onSurfaceVariant
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 11
            }
        }
    }

    Flickable {
        id: categoryViewport
        x: 22
        y: 82
        width: parent.width - 44
        height: 36
        contentWidth: categoryRow.width
        contentHeight: height
        clip: true
        interactive: contentWidth > width

        Row {
            id: categoryRow
            spacing: 7
            property int activeIndex: Math.max(0, categoryNames.indexOf(root.activeCategory))

            function selectAdjacent(direction) {
                activeIndex = (activeIndex + direction + categoryNames.length) % categoryNames.length
                root.activeCategory = categoryNames[activeIndex]
                root.resetSelection()
                categoryViewport.contentX = Math.max(0, Math.min(children[activeIndex].x - 20,
                                                                 categoryViewport.contentWidth - categoryViewport.width))
            }

            Repeater {
                model: categoryNames
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: categoryLabel.implicitWidth + 24
                    height: 32
                    radius: 12
                    color: root.activeCategory === modelData ? colors.primaryContainer
                                                             : Qt.rgba(colors.surface.r, colors.surface.g, colors.surface.b, 0.94)
                    border.width: 1
                    border.color: root.activeCategory === modelData ? colors.primary : colors.outlineVariant

                    Text {
                        id: categoryLabel
                        anchors.centerIn: parent
                        text: modelData.toUpperCase()
                        color: root.activeCategory === modelData ? colors.onPrimaryContainer : colors.onSurfaceVariant
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.6
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            categoryRow.activeIndex = index
                            root.activeCategory = modelData
                            root.resetSelection()
                            keyboard.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    ListView {
        id: carousel
        x: 18
        y: 126
        width: parent.width - 36
        height: 372
        orientation: ListView.Horizontal
        model: root.visibleEntries
        header: Item { width: Math.max(0, (carousel.width - 190) * 0.5); height: 1 }
        footer: Item { width: Math.max(0, (carousel.width - 190) * 0.5); height: 1 }
        cacheBuffer: 1600
        currentIndex: -1
        clip: true
        spacing: 12
        boundsBehavior: Flickable.DragOverBounds
        flickDeceleration: 2600
        maximumFlickVelocity: 1300
        snapMode: ListView.SnapToItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: (width - 190) * 0.5
        preferredHighlightEnd: (width - 190) * 0.5
        highlightMoveDuration: 420
        keyNavigationWraps: true

        delegate: Item {
            id: slot
            required property int index
            required property var modelData
            readonly property bool chosen: ListView.isCurrentItem
            readonly property int distanceFromCurrent: Math.abs(index - carousel.currentIndex)
            readonly property real separation: chosen ? 0
                                               : index < carousel.currentIndex
                                                 ? -root.cardSeparation : root.cardSeparation
            width: 190
            height: carousel.height
            z: chosen ? 20 : cardMouse.containsMouse ? 5 : 1

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: slot.chosen ? root.selectedCardWidth : 154
                height: slot.chosen ? root.selectedCardHeight : 248
                radius: 8
                clip: true
                color: colors.surfaceContainer
                border.width: slot.chosen ? 2 : 1
                border.color: slot.chosen ? colors.primary : colors.outlineVariant
                opacity: slot.chosen ? 1
                                     : slot.distanceFromCurrent === 1
                                       ? cardMouse.containsMouse ? 0.94 : 0.78
                                       : cardMouse.containsMouse ? 0.82 : 0.5
                scale: slot.chosen ? 1
                                   : cardMouse.containsMouse ? 0.94
                                   : slot.distanceFromCurrent === 1 ? 0.9 : 0.84
                transform: Translate {
                    x: slot.separation
                    y: slot.chosen ? 0 : slot.distanceFromCurrent === 1 ? 14 : 28
                    Behavior on x {
                        NumberAnimation { duration: 420; easing.type: Easing.OutQuint }
                    }
                    Behavior on y {
                        NumberAnimation { duration: 360; easing.type: Easing.OutQuint }
                    }
                }

                Behavior on width { NumberAnimation { duration: 380; easing.type: Easing.OutQuint } }
                Behavior on height { NumberAnimation { duration: 380; easing.type: Easing.OutQuint } }
                Behavior on radius { NumberAnimation { duration: 300; easing.type: Easing.OutQuint } }
                Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 340; easing.type: Easing.OutQuint } }
                Behavior on border.color { ColorAnimation { duration: 220 } }

                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: slot.modelData.thumbnailUrl
                    fillMode: Image.PreserveAspectCrop
                    autoTransform: true
                    asynchronous: false
                    cache: true
                    opacity: slot.chosen ? 0.18 : 1
                    Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                }

                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: slot.chosen ? slot.modelData.imageUrl : ""
                    fillMode: Image.PreserveAspectCrop
                    autoTransform: true
                    asynchronous: true
                    cache: true
                    opacity: slot.chosen && status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    gradient: Gradient {
                        GradientStop { position: slot.chosen ? 0.56 : 0.34; color: "#00000000" }
                        GradientStop { position: 1.0; color: slot.chosen ? "#d9000000" : "#e6000000" }
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: slot.chosen ? 20 : 12
                    spacing: slot.chosen ? 5 : 2

                    Text {
                        height: slot.chosen ? implicitHeight : 0
                        opacity: slot.chosen ? 1 : 0
                        clip: true
                        text: slot.modelData.category.toUpperCase() + "  /  " + slot.modelData.detail.toUpperCase()
                        color: colors.primary
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.2
                        Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutQuint } }
                        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    }
                    Text {
                        width: parent.width
                        text: slot.modelData.name
                        elide: Text.ElideRight
                        color: "#ffffff"
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: slot.chosen ? 20 : 10
                        font.weight: Font.Bold
                        Behavior on font.pixelSize { NumberAnimation { duration: 300; easing.type: Easing.OutQuint } }
                    }
                    Text {
                        height: slot.chosen ? implicitHeight : 0
                        opacity: slot.chosen ? 1 : 0
                        clip: true
                        text: "Double-click or press Enter to apply"
                        color: "#c7ffffff"
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: 9
                        Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutQuint } }
                        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    }
                }


                MouseArea {
                    id: cardMouse
                    z: 30
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.selectIndex(slot.index)
                        keyboard.forceActiveFocus()
                    }
                    onDoubleClicked: root.applyCurrent()
                }
            }
    }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 8

        Rectangle {
            width: countText.implicitWidth + 24
            height: 34
            radius: 13
            color: Qt.rgba(colors.surface.r, colors.surface.g, colors.surface.b, 0.96)
            border.width: 1
            border.color: colors.outlineVariant
            Text {
                id: countText
                anchors.centerIn: parent
                text: root.matchCount + " SCENES"
                color: colors.onSurfaceVariant
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 9
                font.weight: Font.Bold
            }
        }
        Rectangle {
            width: indexText.implicitWidth + 24
            height: 34
            radius: 13
            color: colors.primaryContainer
            border.width: 1
            border.color: colors.primary
            Text {
                id: indexText
                anchors.centerIn: parent
                text: String(carousel.currentIndex + 1).padStart(2, "0") + " / "
                      + String(wallpaperEntries.length).padStart(2, "0")
                color: colors.onPrimaryContainer
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 9
                font.weight: Font.Bold
            }
        }
    }
}
