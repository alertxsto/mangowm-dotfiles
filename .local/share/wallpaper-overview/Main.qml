import QtQuick
import QtQuick.Window

Window {
    id: root
    visible: true
    visibility: Window.FullScreen
    flags: Qt.FramelessWindowHint
    title: "Mango Wallpaper Gallery"
    color: colors.surface

    property var colors: themeColors
    property string selectedPath: ""
    property string activeCategory: "All"
    property int matchCount: 0



    function entryAt(index) {
        return index >= 0 && index < wallpaperEntries.length ? wallpaperEntries[index] : null
    }

    function entryMatches(entry) {
        if (!entry)
            return false
        const categoryMatch = activeCategory === "All" || entry.category === activeCategory
        const needle = searchField.text.trim().toLowerCase()
        return categoryMatch && (needle.length === 0 || entry.search.indexOf(needle) !== -1)
    }

    function recount() {
        let count = 0
        for (let index = 0; index < wallpaperEntries.length; ++index)
            if (entryMatches(wallpaperEntries[index]))
                ++count
        matchCount = count
    }

    function firstMatch() {
        for (let index = 0; index < wallpaperEntries.length; ++index)
            if (entryMatches(wallpaperEntries[index]))
                return index
        return -1
    }

    function selectIndex(index) {
        carousel.currentIndex = index
    }

    function moveSelection(direction) {
        if (wallpaperEntries.length === 0)
            return
        let index = carousel.currentIndex
        for (let step = 0; step < wallpaperEntries.length; ++step) {
            index = (index + direction + wallpaperEntries.length) % wallpaperEntries.length
            if (entryMatches(wallpaperEntries[index])) {
                selectIndex(index)
                return
            }
        }
    }

    function resetSelection() {
        recount()
        const index = firstMatch()
        if (index >= 0)
            selectIndex(index)
    }

    function applyCurrent() {
        const entry = entryAt(carousel.currentIndex)
        if (entry && entryMatches(entry)) {
            selectedPath = entry.path
            Qt.quit()
        }
    }

    Image {
        anchors.fill: parent
        source: {
            const entry = root.entryAt(carousel.currentIndex)
            return entry ? entry.thumbnailUrl : ""
        }
        fillMode: Image.PreserveAspectCrop
        asynchronous: false
        opacity: 0.16
    }

    Rectangle {
        anchors.fill: parent
        color: colors.surface
        opacity: 0.88
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#14000000" }
            GradientStop { position: 0.52; color: "#56000000" }
            GradientStop { position: 1.0; color: colors.surface }
        }
    }

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
            } else if (event.text && event.text.length === 1 && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
                searchField.text += event.text
                root.resetSelection()
                event.accepted = true
            }
        }
    }

    Column {
        id: heading
        x: 72
        y: 52
        spacing: 6

        Text {
            text: "WALLPAPER / THEMES"
            color: colors.primary
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 2.4
        }
        Text {
            text: "Pick a scene.\nRecolor the whole desktop."
            color: colors.onSurface
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 32
            font.weight: Font.Bold
            lineHeight: 0.94
        }
    }

    Rectangle {
        id: searchBox
        anchors.right: parent.right
        anchors.rightMargin: 72
        y: 66
        width: 360
        height: 48
        radius: 14
        color: colors.surfaceContainer
        border.width: 1
        border.color: searchField.text.length > 0 ? colors.primary : colors.outlineVariant

        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "󰍉"
            color: colors.primary
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 18
        }
        TextInput {
            id: searchField
            x: 48
            width: parent.width - 64
            anchors.verticalCenter: parent.verticalCenter
            readOnly: true
            color: colors.onSurface
            selectionColor: colors.primaryContainer
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 13
        }
        Text {
            x: 48
            anchors.verticalCenter: parent.verticalCenter
            visible: searchField.text.length === 0
            text: "Type to filter wallpapers"
            color: colors.onSurfaceVariant
            opacity: 0.72
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 13
        }
    }

    Flickable {
        id: categoryViewport
        x: 72
        y: 164
        width: parent.width - 144
        height: 42
        contentWidth: categoryRow.width
        contentHeight: height
        clip: true
        interactive: contentWidth > width

        Row {
            id: categoryRow
            spacing: 8
            property int activeIndex: Math.max(0, categoryNames.indexOf(root.activeCategory))

            function selectAdjacent(direction) {
                activeIndex = (activeIndex + direction + categoryNames.length) % categoryNames.length
                root.activeCategory = categoryNames[activeIndex]
                root.resetSelection()
                categoryViewport.contentX = Math.max(0, Math.min(children[activeIndex].x - 72, categoryViewport.contentWidth - categoryViewport.width))
            }

            Repeater {
                model: categoryNames
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: categoryLabel.implicitWidth + 26
                    height: 34
                    radius: 10
                    color: root.activeCategory === modelData ? colors.primaryContainer : colors.surfaceContainer
                    border.width: 1
                    border.color: root.activeCategory === modelData ? colors.primary : colors.outlineVariant

                    Text {
                        id: categoryLabel
                        anchors.centerIn: parent
                        text: modelData.toUpperCase()
                        color: root.activeCategory === modelData ? colors.onPrimaryContainer : colors.onSurfaceVariant
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
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

    Rectangle {
        id: heroCard
        anchors.horizontalCenter: parent.horizontalCenter
        y: 214
        width: Math.min(840, parent.width * 0.55)
        height: width * 9 / 16
        radius: 22
        clip: true
        color: colors.surfaceContainer
        border.width: 2
        border.color: colors.primary

        Image {
            anchors.fill: parent
            source: {
                const entry = root.entryAt(carousel.currentIndex)
                return entry ? entry.thumbnailUrl : ""
            }
            fillMode: Image.PreserveAspectFit
            asynchronous: false
            cache: true
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.62; color: "#08000000" }
                GradientStop { position: 1.0; color: "#dc000000" }
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            spacing: 6

            Text {
                text: {
                    const entry = root.entryAt(carousel.currentIndex)
                    return entry ? entry.category.toUpperCase() + "  /  " + entry.detail.toUpperCase() : ""
                }
                color: colors.primary
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.4
            }

            Text {
                width: parent.width
                text: {
                    const entry = root.entryAt(carousel.currentIndex)
                    return entry ? entry.name : ""
                }
                elide: Text.ElideRight
                color: "#ffffff"
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 24
                font.weight: Font.Bold
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: keyboard.forceActiveFocus()
            onDoubleClicked: root.applyCurrent()
        }
    }

    NumberAnimation {
        id: heroPulse
        target: heroCard
        property: "scale"
        from: 0.985
        to: 1.0
        duration: 160
        easing.type: Easing.OutCubic
    }

    ListView {
        id: carousel
        x: 72
        y: heroCard.y + heroCard.height + 18
        width: parent.width - 144
        height: 116
        orientation: ListView.Horizontal
        model: wallpaperEntries
        header: Item { width: Math.max(0, carousel.width * 0.5 - 95); height: 1 }
        footer: Item { width: Math.max(0, carousel.width * 0.5 - 95); height: 1 }
        cacheBuffer: 1100
        currentIndex: initialWallpaperIndex
        clip: true
        spacing: 0
        boundsBehavior: Flickable.StopAtBounds
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: width * 0.5 - 95
        preferredHighlightEnd: width * 0.5 + 95
        highlightMoveDuration: 130
        keyNavigationWraps: true
        onCurrentIndexChanged: heroPulse.restart()

        delegate: Item {
            id: cardSlot
            required property int index
            required property var modelData
            property bool chosen: ListView.isCurrentItem
            property bool matches: root.entryMatches(modelData)
            visible: matches
            width: matches ? 190 : 0
            height: carousel.height

            // Animated frame/lift so the cursor is unmistakable while browsing.
            Rectangle {
                anchors.centerIn: card
                width: card.width + 10
                height: card.height + 10
                radius: card.radius + 5
                color: "transparent"
                border.width: 2
                border.color: card.frameColor
                opacity: cardSlot.chosen ? 0.45 : 0.0
                Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            }

            Rectangle {
                id: card
                width: 170
                height: 96
                anchors.centerIn: parent
                anchors.verticalCenterOffset: cardSlot.chosen ? -5 : 0
                radius: 12
                clip: true
                color: colors.surfaceContainer
                property bool hovered: cardMouse.containsMouse
                property color frameColor: cardSlot.chosen ? colors.primary
                                      : hovered ? colors.primary
                                      : colors.outlineVariant
                opacity: cardSlot.chosen ? 1.0 : 0.68
                scale: cardSlot.chosen ? 1.06 : hovered ? 1.02 : 1.0
                border.width: cardSlot.chosen ? 2 : 1
                border.color: frameColor

                Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on frameColor { ColorAnimation { duration: 140 } }
                Behavior on anchors.verticalCenterOffset {
                    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                }

                Image {
                    anchors.fill: parent
                    source: cardSlot.modelData.thumbnailUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: false
                    cache: true
                }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.35; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#c9000000" }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 9
                    text: cardSlot.modelData.name
                    elide: Text.ElideRight
                    color: "#ffffff"
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.selectIndex(cardSlot.index)
                        keyboard.forceActiveFocus()
                    }
                    onDoubleClicked: root.applyCurrent()
                }
            }
        }

        Component.onCompleted: {
            positionViewAtIndex(currentIndex, ListView.Center)
            root.recount()
            keyboard.forceActiveFocus()
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 72
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        spacing: 12

        Rectangle {
            width: countLabel.implicitWidth + 24
            height: 34
            radius: 10
            color: colors.primaryContainer
            Text {
                id: countLabel
                anchors.centerIn: parent
                text: root.matchCount + " SCENES"
                color: colors.onPrimaryContainer
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "↑↓ collection    ←→ browse    type search    ↵ apply    esc close"
            color: colors.onSurfaceVariant
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 11
        }
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 72
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 55
        text: {
            const entry = root.entryAt(carousel.currentIndex)
            return entry ? String(carousel.currentIndex + 1).padStart(2, "0") + " / " + String(wallpaperEntries.length).padStart(2, "0") : "00 / 00"
        }
        color: colors.primary
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 12
        font.weight: Font.Bold
        font.letterSpacing: 1.2
    }
}
