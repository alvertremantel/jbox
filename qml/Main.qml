import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQml

// Bottom-left, frameless, translucent launcher. The KDE look is provided by
// the `org.kde.desktop` QtQuick.Controls style, which we set in main.py.
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
        // Translucent dark panel; a real "KDE" app would pick this from the
        // theme, but a hard-coded translucent value keeps it predictable.
        color: Qt.rgba(0.10, 0.11, 0.13, 0.92)
        radius: 10
        border.color: Qt.rgba(1, 1, 1, 0.08)
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
                    color: "#9ece6a"
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
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: input.activeFocus
                            ? Qt.rgba(0.62, 0.76, 0.94, 0.6)
                            : Qt.rgba(1, 1, 1, 0.06)
                        border.width: 1
                        radius: 6
                    }
                    color: "#e8eef5"
                    placeholderTextColor: "#8a93a3"

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
                            ? Qt.rgba(0.62, 0.76, 0.94, 0.18)
                            : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Label {
                            text: modelData.command !== undefined ? "★" : "↻"
                            color: modelData.command !== undefined ? "#bb9af7" : "#7aa2f7"
                            font.pixelSize: 12
                            Layout.preferredWidth: 16
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.command !== undefined
                                ? (modelData.name + "  →  " + modelData.command)
                                : (modelData.command + "  (" + modelData.count + "×)")
                            color: "#cdd6f4"
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
                color: "#7a8595"
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
                    color: Qt.rgba(0, 0, 0, 0.35)
                    radius: 4
                }
                TextArea {
                    id: outputArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: "#e8eef5"
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
