import QtQuick

pragma Singleton

QtObject {
    id: theme

    readonly property bool dark: true

    // Surfaces
    readonly property color background: "#141413"
    readonly property color surface: "#1c1c1a"
    readonly property color surfaceHover: "#262624"
    readonly property color surfaceActive: "#2f2f2c"
    readonly property color border: "#2c2c29"

    // Text
    readonly property color text: "#e9e7e2"
    readonly property color textMuted: "#8f8c85"
    readonly property color textFaint: "#63615c"
    readonly property color accent: "#c9a96a"
    readonly property color danger: "#d1685e"
    readonly property color success: "#7fa06a"

    // Inline code / code blocks
    readonly property color codeBackground: "#1e1e22"
    readonly property color codeText: "#d8cfb8"
    readonly property string monoFont: "JetBrains Mono, Berkeley Mono, monospace"

    readonly property int contentWidth: 720
    readonly property int spacing: 8
    readonly property int radius: 6

    readonly property int blockSpacing: 2
    readonly property int blockPaddingV: 4
    readonly property int blockMinHeight: 30

    function headingPixelSize(level) {
        switch (level) {
        case "h1": return 30
        case "h2": return 24
        case "h3": return 20
        case "h4": return 18
        default: return 22
        }
    }
}
