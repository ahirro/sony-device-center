import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Small quick-control window that opens from the tray icon. It only talks to the
// shared `controller`; the tray manager positions and shows it.
Window {
    id: pop
    objectName: "trayPopup"

    signal openMainRequested()

    readonly property int pad: 16
    width: 340
    height: column.implicitHeight + 2 * pad
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    title: "Sony Device Center"

    // Same palette as Main.qml.
    readonly property color surface:   "#14161E"
    readonly property color surfaceHi: "#1B1E29"
    readonly property color sunk:      "#0E1016"
    readonly property color line:      "#22252F"
    readonly property color lineHi:    "#2F3341"
    readonly property color accent:    "#7C5CFF"
    readonly property color warm:      "#F2A73B"
    readonly property color success:   "#2DD4A7"
    readonly property color danger:    "#FF5A5F"
    readonly property color txt:       "#F4F6FA"
    readonly property color txtDim:    "#98A1B2"
    readonly property color txtFaint:  "#5C6473"

    readonly property string mode: controller.noiseControlMode
    readonly property bool parked: controller.connectionState === "idle"

    onActiveChanged: if (!active) hide()

    Shortcut { sequence: "Escape"; onActivated: pop.hide() }

    // A pill button used for the noise-control segments and the small actions.
    component Pill: Rectangle {
        id: pill
        property string label: ""
        property bool active: false
        property color tint: pop.accent
        signal clicked()
        implicitHeight: 36
        radius: 10
        color: active ? Qt.rgba(tint.r, tint.g, tint.b, 0.20) : (hover.hovered ? pop.surfaceHi : pop.sunk)
        border.width: 1
        border.color: active ? Qt.rgba(tint.r, tint.g, tint.b, 0.75) : (hover.hovered ? pop.lineHi : pop.line)
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: pill.clicked() }
        Text {
            anchors.centerIn: parent
            text: pill.label
            textFormat: Text.PlainText
            color: pill.active ? pop.txt : pop.txtDim
            font.pixelSize: 12
            font.weight: pill.active ? Font.DemiBold : Font.Normal
        }
    }

    // Label + on/off switch row.
    component ToggleRow: RowLayout {
        id: row
        property string label: ""
        property bool checked: false
        signal toggled()
        Layout.fillWidth: true
        spacing: 10
        Text {
            Layout.fillWidth: true
            text: row.label
            textFormat: Text.PlainText
            color: pop.txt
            font.pixelSize: 13
        }
        Rectangle {
            implicitWidth: 40; implicitHeight: 22; radius: 11
            color: row.checked ? pop.accent : pop.sunk
            border.width: 1
            border.color: row.checked ? pop.accent : pop.lineHi
            Rectangle {
                width: 16; height: 16; radius: 8
                y: 3
                x: row.checked ? parent.width - width - 3 : 3
                color: "#FFFFFF"
                Behavior on x { NumberAnimation { duration: 120 } }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: row.toggled() }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: pop.surface
        border.width: 1
        border.color: pop.lineHi

        ColumnLayout {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: pop.pad
            spacing: 12

            // Header: connection dot, name, battery.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: !controller.connected ? pop.danger : (pop.parked ? pop.warm : pop.success)
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: controller.deviceName.length ? controller.deviceName : "No headset"
                    color: pop.txt
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    textFormat: Text.PlainText
                    visible: controller.batteryLevel >= 0
                    text: controller.batteryLevel + "%" + (controller.isCharging ? "  charging" : "")
                    color: controller.batteryLevel <= 15 && !controller.isCharging ? pop.danger : pop.txtDim
                    font.pixelSize: 13
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                font.pixelSize: 11
                color: controller.lastError.length ? pop.danger : pop.txtFaint
                text: controller.lastError.length ? controller.lastError
                    : controller.busy ? "Working…"
                    : pop.parked ? "Idle: the headset is free for your phone. Any change here reconnects."
                    : controller.connected ? "Connected" : "Connection: " + controller.connectionState
            }

            // Noise control.
            Text {
                visible: controller.hasAnc || controller.hasAmbient
                text: "NOISE CONTROL"
                textFormat: Text.PlainText
                color: pop.txtFaint
                font.pixelSize: 10
                font.letterSpacing: 1.2
            }
            RowLayout {
                visible: controller.hasAnc || controller.hasAmbient
                Layout.fillWidth: true
                spacing: 8
                Pill {
                    Layout.fillWidth: true
                    visible: controller.hasAnc
                    label: "Noise cancel"
                    active: pop.mode === "cancelling"
                    onClicked: controller.setAnc(true)
                }
                Pill {
                    Layout.fillWidth: true
                    visible: controller.hasAmbient
                    label: "Ambient"
                    tint: pop.warm
                    active: pop.mode === "ambient"
                    onClicked: controller.setAmbient(controller.ambientLevel > 0 ? controller.ambientLevel : 10, controller.focusOnVoice)
                }
                Pill {
                    Layout.fillWidth: true
                    label: "Off"
                    tint: pop.txtDim
                    active: pop.mode === "off"
                    onClicked: controller.setNoiseControlOff()
                }
            }

            // Ambient level and voice focus, only while ambient is on.
            ColumnLayout {
                Layout.fillWidth: true
                visible: pop.mode === "ambient"
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Level"
                        textFormat: Text.PlainText
                        color: pop.txtDim
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Math.round(levelSlider.value)
                        textFormat: Text.PlainText
                        color: pop.txt
                        font.pixelSize: 12
                    }
                }
                Slider {
                    id: levelSlider
                    Layout.fillWidth: true
                    from: 1; to: 20; stepSize: 1
                    value: controller.ambientLevel > 0 ? controller.ambientLevel : 10
                    onPressedChanged: if (!pressed) controller.setAmbient(Math.round(value), controller.focusOnVoice)
                    background: Rectangle {
                        x: levelSlider.leftPadding
                        y: levelSlider.topPadding + levelSlider.availableHeight / 2 - height / 2
                        width: levelSlider.availableWidth; height: 4; radius: 2
                        color: pop.line
                        Rectangle { width: levelSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: pop.warm }
                    }
                    handle: Rectangle {
                        x: levelSlider.leftPadding + levelSlider.visualPosition * (levelSlider.availableWidth - width)
                        y: levelSlider.topPadding + levelSlider.availableHeight / 2 - height / 2
                        width: 18; height: 18; radius: 9
                        color: "#FFFFFF"
                        border.width: 2; border.color: pop.warm
                    }
                }
                ToggleRow {
                    label: "Focus on Voice"
                    checked: controller.focusOnVoice
                    onToggled: controller.setAmbient(controller.ambientLevel > 0 ? controller.ambientLevel : 10, !controller.focusOnVoice)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: pop.line
                visible: controller.hasDsee || controller.hasSpeakToChat || controller.hasEqualizer
            }

            ToggleRow {
                visible: controller.hasDsee
                label: "DSEE Extreme"
                checked: controller.dsee
                onToggled: controller.setDsee(!controller.dsee)
            }
            ToggleRow {
                visible: controller.hasSpeakToChat
                label: "Speak-to-Chat"
                checked: controller.speakToChat
                onToggled: controller.setSpeakToChat(!controller.speakToChat)
            }
            RowLayout {
                visible: controller.hasEqualizer
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: "Equalizer: " + controller.equalizerPresetName
                    color: pop.txt
                    font.pixelSize: 13
                }
                Pill {
                    visible: controller.equalizerPreset > 0
                    implicitWidth: 56
                    implicitHeight: 28
                    label: "Reset"
                    tint: pop.txtDim
                    onClicked: controller.setEqualizerPreset(0)
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: pop.line }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Pill {
                    Layout.fillWidth: true
                    label: "Open Device Center"
                    onClicked: { pop.hide(); pop.openMainRequested() }
                }
                Pill {
                    implicitWidth: 64
                    label: "Quit"
                    tint: pop.danger
                    onClicked: Qt.quit()
                }
            }
        }
    }
}
