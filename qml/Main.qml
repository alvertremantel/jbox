import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQml

// Bottom-left, frameless, translucent launcher. The `org.kde.desktop`
// QtQuick.Controls style (set in main.cpp) gives native control chrome that
// tracks the OS's Application Style; the SystemPalette below tracks colors
// from the OS's Global Theme (dark/light + accent). Both matter and are
// independent settings in KDE — don't hard-code colors here.
ApplicationWindow {
    id: root

    width: 540
    minimumWidth: 360
    maximumWidth: 720
    height: column.implicitHeight + 24
    minimumHeight: column.implicitHeight + 24

    // Tool windows have no taskbar entry; combine with frameless + on-top
    // for the typical launcher presentation.
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    // Per-pixel transparency — QML elements must use their own alpha too.
    color: "#00000000"
    title: "jbox"

    // --- Positioning ------------------------------------------------------
    // Pin to the bottom-left of the primary screen, with a small margin.
    // Reposition on completion, when the window becomes visible, and on a
    // slow safety timer (catches hot-plug of monitors).
    function _reposition() {
        if (!visible) return
        // `Screen` is the QML attached property that exposes the screen the
        // window is currently on. desktopAvailableX/Y/W/H account for any
        // system bars (panel, dock) and are the right thing to anchor to.
        var ax = Screen.virtualX || 0
        var ay = Screen.virtualY || 0
        var aw = (Screen.desktopAvailableWidth  || (Screen.width  || 1024))
        var ah = (Screen.desktopAvailableHeight || (Screen.height || 768))
        // Some platforms report available geometry as an offset origin; use
        // virtualX/Y + the available extent for a robust bottom-left.
        var availBottom = ay + ah
        var availRight = ax + aw
        var m = 16
        var w = Math.min(root.width, aw - 2 * m)
        if (w < 200) w = Math.min(root.width, aw)
        root.width = w
        root.x = ax + m
        root.y = availBottom - root.height - m
    }
    onScreenChanged: _reposition()
    onVisibleChanged: _reposition()
    Component.onCompleted: _reposition()

    // Cheap safety net for monitor hot-plug (connect/disconnect events
    // don't fire a geometryChanged on the previously-anchored screen).
    Timer {
        interval: 5000
        running: visible
        repeat: true
        onTriggered: _reposition()
    }

    // Tracks the live KDE color scheme (Global Theme), independent of the
    // QQC2 control style (Application Style, set via --style). Panel/text
    // colors below derive from this instead of being hard-coded, so jbox
    // follows dark/light + accent-color changes like any other KDE app.
    SystemPalette {
        id: palette
        colorGroup: SystemPalette.Active
    }

    // --- State ------------------------------------------------------------
    property int selectedSuggestion: -1
    property var visibleSuggestions: []
    property bool showOutput: false
    property string lastOutput: ""

    Connections {
        target: backend
        function onStatusChanged(text) { statusLabel.text = text }
        function onOutputReceived(text) {
            lastOutput = text
            showOutput = true
            outputArea.text = text
        }
    }

    // --- Layout -----------------------------------------------------------
    Rectangle {
        id: panel
        anchors.fill: parent
        // Translucent panel tinted from the active color scheme, so it goes
        // dark/light/accented along with the rest of the desktop.
        color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b, 0.65)
        radius: 0
        // Opaque: KWin's blur-behind region is the window's full rectangle,
        // so a rounded/translucent border here leaves the corners showing
        // raw blurred wallpaper with no panel tint over them.
        border.color: Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 0.75)
        border.width: 1

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            // --- Input row -------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "❯"
                    color: palette.highlight
                    font.pixelSize: 18
                    font.family: "monospace"
                    Layout.alignment: Qt.AlignVCenter
                }

                TextField {
                    id: input
                    Layout.fillWidth: true
                    placeholderText: "Type a command…  Enter=terminal · Shift+Enter=capture · Esc=hide"
                    font.pixelSize: 16
                    selectByMouse: true
                    background: Rectangle {
                        color: Qt.rgba(palette.shadow.r, palette.shadow.g, palette.shadow.b, 0.80)
                        border.color: input.activeFocus
                            ? Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.6)
                            : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.06)
                        border.width: 1
                        radius: 6
                    }
                    color: palette.text
                    placeholderTextColor: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.75)

                    // Keep the input focused whenever the window is shown.
                    onActiveFocusChanged: {
                        if (!activeFocus && root.visible) {
                            input.forceActiveFocus()
                        }
                    }

                    onTextChanged: {
                        root.selectedSuggestion = -1
                        _refreshSuggestions()
                    }

                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (event.modifiers & Qt.ShiftModifier) {
                                _execute(true)   // Shift+Enter → capture
                            } else {
                                _execute(false)  // plain Enter → terminal
                            }
                            event.accepted = true
                            return
                        }

                        if (event.key === Qt.Key_Escape) {
                            root.hide()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down) {
                            if (root.visibleSuggestions.length > 0) {
                                root.selectedSuggestion =
                                    (root.selectedSuggestion + 1) % root.visibleSuggestions.length
                                event.accepted = true
                            }
                        } else if (event.key === Qt.Key_Up) {
                            if (root.visibleSuggestions.length > 0) {
                                root.selectedSuggestion =
                                    (root.selectedSuggestion - 1 + root.visibleSuggestions.length)
                                    % root.visibleSuggestions.length
                                event.accepted = true
                            }
                        } else if (event.key === Qt.Key_Tab) {
                            // Tab to autocomplete from the selected suggestion.
                            if (root.selectedSuggestion >= 0
                                && root.selectedSuggestion < root.visibleSuggestions.length) {
                                var s = root.visibleSuggestions[root.selectedSuggestion]
                                if (s.command !== undefined) {
                                    // Alias suggestion
                                    input.text = s.name
                                } else {
                                    input.text = s.command
                                }
                                input.cursorPosition = input.text.length
                                _refreshSuggestions()
                            }
                            event.accepted = true
                        }
                    }
                }
            }

            // --- Suggestions ----------------------------------------------
            ListView {
                id: suggestionList
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                visible: count > 0
                interactive: false
                clip: true
                spacing: 0
                model: root.visibleSuggestions

                delegate: ItemDelegate {
                    width: suggestionList.width
                    height: 28
                    highlighted: index === root.selectedSuggestion
                    background: Rectangle {
                        color: highlighted
                            ? Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.25)
                            : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Label {
                            text: modelData.command !== undefined ? "★" : "↻"
                            color: modelData.command !== undefined
                                ? palette.highlight
                                : Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                            font.pixelSize: 12
                            Layout.preferredWidth: 16
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.command !== undefined
                                ? (modelData.name + "  →  " + modelData.command)
                                : (modelData.command + "  (" + modelData.count + "×)")
                            color: palette.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                    onClicked: {
                        if (modelData.command !== undefined) {
                            input.text = modelData.name
                        } else {
                            input.text = modelData.command
                        }
                        input.cursorPosition = input.text.length
                        input.forceActiveFocus()
                    }
                }
            }

            // --- Status / output ------------------------------------------
            Label {
                id: statusLabel
                Layout.fillWidth: true
                text: "ready"
                color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.6)
                font.pixelSize: 11
                elide: Text.ElideRight
                visible: !root.showOutput
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, outputArea.implicitHeight + 16)
                visible: root.showOutput
                clip: true
                background: Rectangle {
                    color: Qt.rgba(palette.shadow.r, palette.shadow.g, palette.shadow.b, 0.35)
                    radius: 4
                }
                TextArea {
                    id: outputArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: palette.text
                    font.family: "monospace"
                    font.pixelSize: 12
                    background: null
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.showOutput
                Item { Layout.fillWidth: true }
                Button {
                    text: "clear"
                    flat: true
                    onClicked: { root.showOutput = false; outputArea.text = "" }
                }
            }
        }
    }

    // --- Focus-loss hides the window --------------------------------------
    // ApplicationWindow doesn't emit a single "active" signal in QML, so we
    // approximate: when the window's activeFocus changes to false, hide it.
    onActiveFocusItemChanged: {
        if (activeFocusItem === null) {
            // Don't fight with programmatic focus changes during hide().
            hideTimer.restart()
        }
    }

    Timer {
        id: hideTimer
        interval: 80
        repeat: false
        onTriggered: {
            if (root.activeFocusItem === null && !input.activeFocus) {
                root.hide()
            }
        }
    }

    // --- Helpers ----------------------------------------------------------
    function _refreshSuggestions() {
        var q = input.text.trim()
        if (q.length === 0) {
            root.visibleSuggestions = []
            return
        }
        // Aliases first (more relevant to this app), then history.
        var aliases = backend.suggestAliases(q)
        var history = backend.suggestHistory(q)
        root.visibleSuggestions = aliases.concat(history)
        root.selectedSuggestion = root.visibleSuggestions.length > 0 ? 0 : -1
    }

    function _execute(capture) {
        var toRun = input.text.trim()
        if (toRun.length === 0) return
        if (capture) {
            backend.runCapture(toRun)
            // Stay visible so the user can see the captured output.
        } else {
            backend.run(toRun)
            // run() launches a terminal window; hide jbox.
            root.hide()
        }
        input.text = ""
        root.selectedSuggestion = -1
        root.visibleSuggestions = []
    }
}
