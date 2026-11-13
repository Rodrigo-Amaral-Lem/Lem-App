import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "signatureDialog"
    property string checkpoint: ""
    property string signerName: ""
    signal saveRequested(string checkpoint, string png)
    title: "Assinatura do técnico"
    modal: true
    width: Math.min(720, parent ? parent.width - 24 : 720)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(12, (parent.height - height) / 2) : 0
    closePolicy: Popup.CloseOnEscape
    onOpened: signatureCanvas.clearInk()
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: dialog.signerName
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "Assine com o dedo ou com o rato na área abaixo."
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(240, dialog.parent ? dialog.parent.height * 0.32 : 240)
            color: "white"
            border.color: "#94a3b8"
            clip: true
            Canvas {
                id: signatureCanvas
                objectName: "signatureCanvas"
                anchors.fill: parent
                anchors.margins: 2
                property int marks: 0
                property var points: []
                function clearInk() {
                    points = [];
                    marks = 0;
                    requestPaint();
                }
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.fillStyle = "white";
                    ctx.fillRect(0, 0, width, height);
                    ctx.strokeStyle = "#172b4d";
                    ctx.lineWidth = 3;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";
                    ctx.beginPath();
                    for (let i = 0; i < points.length; i++) {
                        const p = points[i];
                        if (p.start) ctx.moveTo(p.x * width, p.y * height);
                        else ctx.lineTo(p.x * width, p.y * height);
                    }
                    ctx.stroke();
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onPressed: function(mouse) {
                        signatureCanvas.points.push({x: mouse.x / width, y: mouse.y / height, start: true});
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed || signatureCanvas.points.length > 20000) return;
                        signatureCanvas.points.push({x: mouse.x / width, y: mouse.y / height, start: false});
                        signatureCanvas.marks++;
                        signatureCanvas.requestPaint();
                    }
                }
                Accessible.name: "Área para desenhar a assinatura"
            }
        }
        Label {
            text: "A assinatura será guardada como imagem no report."
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 12
        }
    }
    footer: DialogButtonBox {
        Button {
            implicitHeight: 48
            font.pixelSize: 16
            padding: 10
            text: "Limpar"
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
            onClicked: signatureCanvas.clearInk()
        }
        Button {
            implicitHeight: 48
            font.pixelSize: 16
            padding: 10
            text: "Cancelar"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
        Button {
            implicitHeight: 48
            font.pixelSize: 16
            padding: 10
            objectName: "saveSignature"
            text: "Guardar"
            enabled: signatureCanvas.marks >= 3
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.saveRequested(dialog.checkpoint, signatureCanvas.toDataURL("image/png"))
        }
    }
}
