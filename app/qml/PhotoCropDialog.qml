// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Selection coordinates refer to the oriented image, never its letterboxed area.
// Preview is bounded; the backend crops the full resolution original off-thread.
Dialog {
    id: crop
    objectName: "photoCropDialog"
    property string photoId: ""
    property string reportId: ""
    property url photoSource: ""
    signal applyCrop(string reportId, string photoId, real x, real y, real width, real height)
    property real leftEdge: 0.1
    property real topEdge: 0.1
    property real rightEdge: 0.9
    property real bottomEdge: 0.9
    readonly property real handlePadding: 28
    modal: true
    title: "Recortar fotografia"
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
    onOpened: resetSelection()
    onClosed: photoSource = ""
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Arraste os cantos para recortar ou o centro para mover a seleção."
        }
        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Rectangle { anchors.fill: parent; color: "#18252b" }
            Image {
                id: preview
                anchors.fill: parent
                anchors.margins: crop.handlePadding
                source: crop.photoSource
                fillMode: Image.PreserveAspectFit
                autoTransform: true
                asynchronous: true
                sourceSize: Qt.size(1600, 1600)
            }
            Item {
                id: imageArea
                objectName: "cropImageArea"
                width: preview.paintedWidth
                height: preview.paintedHeight
                anchors.centerIn: parent
                visible: preview.status === Image.Ready
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
    footer: DialogButtonBox {
        Button { objectName: "resetPhotoCrop"; implicitHeight: 48; font.pixelSize: 16; text: "Repor"; DialogButtonBox.buttonRole: DialogButtonBox.ResetRole; onClicked: crop.resetSelection() }
        Button { objectName: "cancelPhotoCrop"; implicitHeight: 48; font.pixelSize: 16; text: "Cancelar"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: crop.close() }
        Button {
            objectName: "applyPhotoCrop"; implicitHeight: 48; font.pixelSize: 16
            text: "Recortar"; highlighted: true
            enabled: preview.status === Image.Ready
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: {
                crop.applyCrop(crop.reportId, crop.photoId, crop.leftEdge, crop.topEdge,
                               crop.rightEdge-crop.leftEdge, crop.bottomEdge-crop.topEdge)
                crop.close()
            }
        }
    }
}
