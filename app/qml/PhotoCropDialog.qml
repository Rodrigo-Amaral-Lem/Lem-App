// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Coordinates refer to the EXIF-oriented image after the preview rotation.
// Only Save changes the evidence; the backend rotates/crops the original once.
Dialog {
    id: crop
    objectName: "photoCropDialog"
    property string photoId: ""
    property string reportId: ""
    property url photoSource: ""
    property bool canApply: true
    property bool cropEnabled: false
    property int quarterTurns: 0
    readonly property bool sideways: quarterTurns % 2 !== 0
    signal applyEdit(string reportId, string photoId, int quarterTurns, real x, real y, real width, real height)
    property real leftEdge: 0
    property real topEdge: 0
    property real rightEdge: 1
    property real bottomEdge: 1
    readonly property real handlePadding: 28
    modal: true
    title: "Editar fotografia"
    // Use the stationary window's exclusion margins; the moving popup must not
    // calculate its own margins (which would feed back into its position).
    property real safeTop: parent ? parent.SafeArea.margins.top : 0
    property real safeBottom: parent ? parent.SafeArea.margins.bottom : 0
    property real safeLeft: parent ? parent.SafeArea.margins.left : 0
    property real safeRight: parent ? parent.SafeArea.margins.right : 0
    popupType: Popup.Item
    topMargin: safeTop + 8
    bottomMargin: safeBottom + 8
    leftMargin: safeLeft + 8
    rightMargin: safeRight + 8
    width: Math.min(820, Math.max(1, parent.width - leftMargin - rightMargin))
    height: Math.min(820, Math.max(1, parent.height - topMargin - bottomMargin))
    x: leftMargin + Math.max(0, (parent.width - leftMargin - rightMargin - width) / 2)
    y: topMargin + Math.max(0, (parent.height - topMargin - bottomMargin - height) / 2)
    closePolicy: Popup.CloseOnEscape
    function resetSelection() { leftEdge = 0; topEdge = 0; rightEdge = 1; bottomEdge = 1 }
    function resetEdit() { quarterTurns = 0; cropEnabled = false; resetSelection() }
    function rotateRight() {
        // Carry the selected pixels with the image instead of selecting a new area.
        const l = leftEdge, t = topEdge, r = rightEdge, b = bottomEdge
        leftEdge = 1 - b; topEdge = l; rightEdge = 1 - t; bottomEdge = r
        quarterTurns = (quarterTurns + 1) % 4
    }
    onOpened: resetEdit()
    onClosed: photoSource = ""
    // Match the other touch actions: drawn icons, 48 px targets and a distinct
    // checked state. Text remains available to accessibility and tooltips only.
    component PhotoToolButton: Button {
        id: tool
        required property string symbol
        implicitWidth: 48
        implicitHeight: 48
        Layout.preferredWidth: 48
        Layout.preferredHeight: 48
        Accessible.name: text
        ToolTip.visible: hovered || down
        ToolTip.text: text
        background: Rectangle {
            radius: 8
            color: tool.checked ? "#007f95" : tool.down ? "#e0f0f3" : "white"
            border.color: tool.checked || tool.activeFocus ? "#007f95" : "#78909c"
            border.width: tool.activeFocus ? 2 : 1
            opacity: tool.enabled ? 1 : .45
        }
        contentItem: Item {
            Canvas {
                objectName: tool.symbol === "rotate" ? "rotatePhotoIcon" : "cropPhotoIcon"
                width: 24; height: 24
                anchors.centerIn: parent
                property color strokeColor: tool.checked ? "white" : "#263238"
                opacity: tool.enabled ? 1 : .45
                onStrokeColorChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = strokeColor
                    ctx.lineWidth = 2
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    if (tool.symbol === "rotate") {
                        // Avoid Unicode arrows: Android font coverage varies.
                        ctx.arc(12, 13, 8, Math.PI * .15, Math.PI * 1.75)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(12, 7.34); ctx.lineTo(17.66, 7.34); ctx.lineTo(17.66, 1.7)
                    } else {
                        // Same crossed-corner crop symbol used by the former crop action.
                        ctx.moveTo(6.72, 1.2); ctx.lineTo(6.72, 18); ctx.lineTo(22.8, 18)
                        ctx.moveTo(1.2, 6.72); ctx.lineTo(18, 6.72); ctx.lineTo(18, 22.8)
                    }
                    ctx.stroke()
                }
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            PhotoToolButton {
                objectName: "rotatePhotoPreview"
                symbol: "rotate"
                text: "Rodar 90° para a direita"
                Accessible.name: "Rodar fotografia 90° para a direita"
                enabled: preview.status === Image.Ready
                onClicked: crop.rotateRight()
            }
            PhotoToolButton {
                objectName: "enablePhotoCrop"
                symbol: "crop"
                text: "Recortar"
                checkable: true
                checked: crop.cropEnabled
                enabled: preview.status === Image.Ready
                onToggled: crop.cropEnabled = checked
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: crop.cropEnabled
                ? "Arraste os cantos para recortar ou o centro para mover a seleção."
                : "Pode rodar a fotografia. Ative «Recortar» se precisar."
        }
        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Rectangle { anchors.fill: parent; color: "#18252b" }
            Image {
                id: preview
                anchors.centerIn: parent
                // Fit the rotated bounding box, keeping the handles inside the stage.
                width: Math.max(1, (crop.sideways ? stage.height : stage.width) - 2 * crop.handlePadding)
                height: Math.max(1, (crop.sideways ? stage.width : stage.height) - 2 * crop.handlePadding)
                rotation: crop.quarterTurns * 90
                source: crop.photoSource
                fillMode: Image.PreserveAspectFit
                autoTransform: true
                asynchronous: true
                cache: false
                sourceSize: Qt.size(1600, 1600)
            }
            Item {
                id: imageArea
                objectName: "cropImageArea"
                width: crop.sideways ? preview.paintedHeight : preview.paintedWidth
                height: crop.sideways ? preview.paintedWidth : preview.paintedHeight
                anchors.centerIn: parent
                visible: crop.cropEnabled && preview.status === Image.Ready
                Rectangle { x: 0; y: 0; width: parent.width; height: crop.topEdge * parent.height; color: "#99000000" }
                Rectangle { x: 0; y: crop.bottomEdge * parent.height; width: parent.width; height: parent.height - y; color: "#99000000" }
                Rectangle { x: 0; y: crop.topEdge * parent.height; width: crop.leftEdge * parent.width; height: (crop.bottomEdge - crop.topEdge) * parent.height; color: "#99000000" }
                Rectangle { x: crop.rightEdge * parent.width; y: crop.topEdge * parent.height; width: parent.width - x; height: (crop.bottomEdge - crop.topEdge) * parent.height; color: "#99000000" }
                Rectangle {
                    id: selection
                    x: crop.leftEdge * imageArea.width
                    y: crop.topEdge * imageArea.height
                    width: (crop.rightEdge - crop.leftEdge) * imageArea.width
                    height: (crop.bottomEdge - crop.topEdge) * imageArea.height
                    color: "transparent"
                    border.color: "white"
                    border.width: 2
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            objectName: "cropHandle_" + index
                            width: 24; height: 24; radius: 4
                            color: "#00abc0"; border.color: "white"
                            x: (index % 2 ? selection.width : 0) - width / 2
                            y: (index >= 2 ? selection.height : 0) - height / 2
                        }
                    }
                }
                MouseArea {
                    objectName: "cropTouchArea"
                    x: -crop.handlePadding
                    y: -crop.handlePadding
                    width: imageArea.width + 2 * crop.handlePadding
                    height: imageArea.height + 2 * crop.handlePadding
                    preventStealing: true
                    property real px
                    property real py
                    property real l
                    property real t
                    property real r
                    property real b
                    property int corner: -1
                    onPressed: mouse => {
                        px = mouse.x - crop.handlePadding; py = mouse.y - crop.handlePadding
                        l = crop.leftEdge; t = crop.topEdge; r = crop.rightEdge; b = crop.bottomEdge
                        corner = -1
                        let best = crop.handlePadding * crop.handlePadding
                        for (let n = 0; n < 4; ++n) {
                            let cx = (n % 2 ? r : l) * imageArea.width
                            let cy = (n >= 2 ? b : t) * imageArea.height
                            let distance = (cx - px) ** 2 + (cy - py) ** 2
                            if (distance < best) { best = distance; corner = n }
                        }
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || imageArea.width <= 0 || imageArea.height <= 0) return
                        let dx = (mouse.x - crop.handlePadding - px) / imageArea.width
                        let dy = (mouse.y - crop.handlePadding - py) / imageArea.height
                        if (corner < 0) {
                            dx = Math.max(-l, Math.min(1-r, dx)); dy = Math.max(-t, Math.min(1-b, dy))
                            crop.leftEdge = l+dx; crop.rightEdge = r+dx
                            crop.topEdge = t+dy; crop.bottomEdge = b+dy
                        } else {
                            if (corner % 2) crop.rightEdge = Math.max(l+0.05, Math.min(1, r+dx))
                            else crop.leftEdge = Math.max(0, Math.min(r-0.05, l+dx))
                            if (corner >= 2) crop.bottomEdge = Math.max(t+0.05, Math.min(1, b+dy))
                            else crop.topEdge = Math.max(0, Math.min(b-0.05, t+dy))
                        }
                    }
                }
            }
            Label { anchors.centerIn: parent; visible: preview.status === Image.Error; text: "Não foi possível ler a fotografia"; color: "white" }
        }
    }
    footer: RowLayout {
        // Share the available width even on 320px screens and with cutouts.
        spacing: 6
        Button { Layout.fillWidth: true; Layout.minimumWidth: 0; implicitWidth: 0; objectName: "resetPhotoCrop"; implicitHeight: 48; font.pixelSize: 16; text: "Repor"; DialogButtonBox.buttonRole: DialogButtonBox.ResetRole; onClicked: crop.resetEdit() }
        Button { Layout.fillWidth: true; Layout.minimumWidth: 0; implicitWidth: 0; objectName: "cancelPhotoCrop"; implicitHeight: 48; font.pixelSize: 16; text: "Cancelar"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: crop.close() }
        Button {
            Layout.fillWidth: true; Layout.minimumWidth: 0; implicitWidth: 0
            objectName: "applyPhotoCrop"; implicitHeight: 48; font.pixelSize: 16
            text: "Guardar"; highlighted: true
            enabled: preview.status === Image.Ready && crop.canApply
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: {
                crop.applyEdit(crop.reportId, crop.photoId, crop.quarterTurns,
                               crop.cropEnabled ? crop.leftEdge : 0,
                               crop.cropEnabled ? crop.topEdge : 0,
                               crop.cropEnabled ? crop.rightEdge - crop.leftEdge : 1,
                               crop.cropEnabled ? crop.bottomEdge - crop.topEdge : 1)
                // The caller closes only after the backend accepts the edit.
            }
        }
    }
}
