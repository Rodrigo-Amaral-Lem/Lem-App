// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page
    component OrderButton: Button {
        id: button
        property bool primary: false
        property bool subtle: false
        implicitHeight: Math.max(48, contentItem.implicitHeight + 12)
        leftPadding: 12
        rightPadding: 12
        contentItem: Text {
            text: button.text
            color: !button.enabled ? "#67767b" : button.primary ? "white" : button.subtle ? "#006875" : "#20353c"
            font: button.font
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitWidth: button.subtle ? 48 : 78
            radius: 6
            color: !button.enabled ? "#eef1f2" : button.primary ? (button.down ? "#005e6a" : "#007383") :
                   button.down ? "#d6ecea" : button.subtle ? "transparent" : "white"
            border.color: button.activeFocus ? "#007d83" : "#a2b3b9"
            border.width: button.activeFocus ? 2 : button.subtle || button.primary ? 0 : 1
        }
    }
    component DirectionButton: OrderButton {
        id: directionButton
        property bool pointsUp: false
        subtle: true
        contentItem: Item {
            implicitWidth: directionRow.implicitWidth
            implicitHeight: directionRow.implicitHeight
            RowLayout {
                id: directionRow
                anchors.centerIn: parent
                spacing: 7
                Text {
                    visible: !!directionButton.text
                    text: directionButton.text
                    font: directionButton.font
                    color: "#006875"
                }
                Canvas {
                    id: directionMark
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    readonly property bool up: directionButton.pointsUp
                    readonly property color ink: directionButton.enabled ? "#006875" : "#67767b"
                    onUpChanged: requestPaint()
                    onInkChanged: requestPaint()
                    onPaint: {
                        const c = getContext("2d");
                        c.reset();
                        c.strokeStyle = ink;
                        c.lineWidth = 2;
                        c.lineCap = "round";
                        c.lineJoin = "round";
                        c.translate(width / 2, height / 2);
                        if (up) c.rotate(Math.PI);
                        c.beginPath();
                        c.moveTo(-5, -2);
                        c.lineTo(0, 3);
                        c.lineTo(5, -2);
                        if (!directionButton.text) {
                            c.moveTo(0, 3);
                            c.lineTo(0, -6);
                        }
                        c.stroke();
                    }
                }
            }
        }
    }
    required property var controller
    signal openReport(string target, string report)
    readonly property bool admin: controller.user.role === "admin"
    readonly property var orders: controller.workOrders || []
    readonly property var selected: orders.find(o => o.id === selectedId) || null
    property string selectedId: ""
    property string mode: "list"
    property var plan: ({})
    property var editBase: null
    property var planConflicts: []
    readonly property bool needsPlanReview: mode === "edit" && !!editBase &&
        !!selected && selected.revision !== editBase.revision
    readonly property bool planUnavailable: mode === "edit" && !!editBase &&
        (!selected || !!selected.cancelled_at || !!selected.completed)
    property var technicians: []
    property var targets: []
    property string cancelId: ""
    property int cancelRevision: 0
    property string invalidId: ""
    // Keep an unsent observation if another technician changes the order during editing.
    property string invalidNote: ""
    property string expanded: ""
    property string searchTerm: ""
    readonly property var candidates: controller.workCandidates || []
    readonly property string owner: controller.user.id || ""
    onOwnerChanged: { mode = "list"; selectedId = ""; plan = {}; editBase = null; planConflicts = []; targets = []; technicians = []; invalidId = ""; expanded = ""; }
    onOrdersChanged: if (mode === "detail" && selectedId && !orders.some(o => o.id === selectedId)) {
        selectedId = "";
        mode = "list";
    }
    onVisibleChanged: if (visible && controller.authenticated) controller.refreshOrders()
    onModeChanged: { cancelId = ""; contentItem.contentY = 0; invalidId = ""; expanded = ""; }
    clip: true
    contentWidth: availableWidth
    function goBack() {
        if (!controller.savingOrder) mode = mode === "edit" && selectedId ? "detail" : "list"
    }
    function choose(id) { selectedId = id; mode = "detail"; }
    function edit(order) {
        controller.refreshOrders(true)
        plan = order ? {id: order.id, revision: order.revision} : {id: controller.newOrderId(), revision: 0}
        editBase = order
        planConflicts = []
        technicians = order ? technicianIds(order) : []
        targets = order ? order.items.slice() : []
        start.text = order ? order.start : controller.planningDay
        due.text = order ? order.due : controller.planningDay
        search.text = ""
        mode = "edit"
    }
    function technicianIds(order) {
        return order.technicians.filter(t => !t.role || t.role === "tech").map(t => t.id).sort()
    }
    function reviewPlan() {
        const current = selected
        if (!needsPlanReview || planUnavailable || planConflicts.length) return

        // Compare each editable field with the revision the dispatcher opened.
        // Disjoint edits combine automatically. Overlapping edits need a choice.
        const ids = rows => rows.map(row => row.target)
        const fields = [
            {key: "start", label: "Data de início", base: editBase.start, local: start.text, remote: current.start, value: current.start, display: current.start},
            {key: "due", label: "Prazo", base: editBase.due, local: due.text, remote: current.due, value: current.due, display: current.due},
            {key: "technicians", label: "Técnicos", base: technicianIds(editBase), local: technicians.slice().sort(),
                remote: technicianIds(current), value: technicianIds(current), display: current.technicians.map(t => t.name).join(", ")},
            {key: "targets", label: "Sequência de trabalho", base: ids(editBase.items), local: ids(targets),
                remote: ids(current.items), value: current.items, display: current.items.map(taskLabel).join("\n")}
        ]
        const conflicts = []
        for (const field of fields) {
            const before = JSON.stringify(field.base)
            const local = JSON.stringify(field.local)
            const remote = JSON.stringify(field.remote)
            if (local === before)
                usePlanValue(field)
            else if (remote !== before && local !== remote)
                conflicts.push(field)
        }
        targets = targets.map(row => current.items.find(item => item.target === row.target) || row)
        planConflicts = conflicts
        editBase = current
        plan = {id: current.id, revision: current.revision}
    }
    function usePlanValue(field) {
        switch (field.key) {
        case "start": start.text = field.value; break
        case "due": due.text = field.value; break
        case "technicians": technicians = field.value; break
        case "targets": targets = field.value.slice(); break
        }
    }
    function resolvePlanConflict(field, useCurrent) {
        if (useCurrent) usePlanValue(field)
        planConflicts = planConflicts.filter(conflict => conflict.key !== field.key)
    }
    function move(index, step) {
        let rows = targets.slice()
        rows.splice(index + step, 0, rows.splice(index, 1)[0])
        targets = rows
    }
    function result(order) {
        if (order.cancelled_at) return "Cancelada"
        const done = order.items.filter(i => i.state !== "pending").length
        return done + " / " + order.items.length + " · " + (order.completed ? "Concluída" :
            !order.started ? "Agendada" : order.due < controller.planningDay ? "Em atraso" : "Em curso")
    }
    function localTime(value) { return value ? new Date(value).toLocaleString(Qt.locale("pt_PT")) : "" }
    function taskLabel(item) {
        const kind = item.kind === "commissioning" ? "Comissionamento" :
                     item.kind === "corrective" ? "Corretiva" : "Preventiva"
        const reference = item.reference ? " " + item.reference : ""
        const contract = item.contract ? " · " + item.contract : ""
        return item.external_id + " · " + kind + reference + contract
    }
    Connections {
        target: page.controller
        ignoreUnknownSignals: true
        function onWorkOrderSaved(id) { page.cancelId = ""; page.invalidId = ""; page.choose(id); }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; font.bold: true; font.pixelSize: 20; text: page.mode === "list" ? "Ordens de trabalho" : page.mode === "edit" ? (page.plan.revision ? "Editar ordem" : "Criar ordem") : (page.selected || {}).code || "Ordem"; wrapMode: Text.Wrap }
            OrderButton { visible: page.mode !== "list"; text: "Voltar"; enabled: !controller.savingOrder; onClicked: page.goBack() }
        }
        Label { Layout.fillWidth: true; visible: !!text; wrapMode: Text.Wrap; text: controller.orderMessage || "" }
        RowLayout {
            visible: page.mode === "list"
            OrderButton { text: "Atualizar"; enabled: controller.authenticated; onClicked: controller.refreshOrders() }
            OrderButton { text: "Nova ordem"; visible: page.admin; enabled: controller.authenticated && !!controller.planningDay; onClicked: { page.selectedId = ""; page.edit(null); } }
        }
        Label { visible: page.mode === "list" && !page.orders.length; text: "Sem ordens de trabalho."; wrapMode: Text.Wrap }
        Repeater {
            model: page.mode === "list" ? page.orders : []
            delegate: OrderButton {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.code + "   ·   " + page.result(modelData) + "\n" + modelData.start + " — " + modelData.due
                onClicked: page.choose(modelData.id)
            }
        }
        ColumnLayout {
            id: recoveryList
            Layout.fillWidth: true
            visible: page.mode === "list" && reports.length > 0
            readonly property var reports: controller.localRecoveryReports || []
            Label {
                text: "Cópias locais para recuperação"
                font.bold: true
            }
            Repeater {
                model: recoveryList.reports
                delegate: OrderButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.identifier
                    enabled: !controller.busy
                    onClicked: {
                        if (controller.prepareLocalRecovery(modelData.id))
                            page.openReport(modelData.charger_id, modelData.id)
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            visible: page.mode === "detail" && !!page.selected
            readonly property var order: page.selected || {items: [], technicians: []}
            id: detail
            columns: page.availableWidth >= 720 ? 2 : 1
            columnSpacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: detail.columns === 2 ? 220 : -1
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: page.selected ? page.result(detail.order) + "\nInício " + detail.order.start + " · Prazo " + detail.order.due + "\n" + detail.order.technicians.map(t => t.name).join(", ") : "" }
                Label { Layout.fillWidth: true; visible: !!detail.order.completed; text: "Fecho · " + page.localTime(detail.order.completed); wrapMode: Text.Wrap }
                Label { Layout.fillWidth: true; visible: !!detail.order.cancelled_at; text: "Cancelamento · " + page.localTime(detail.order.cancelled_at); wrapMode: Text.Wrap }
                RowLayout {
                    visible: page.admin && !detail.order.cancelled_at && (!detail.order.completed || detail.order.following)
                    OrderButton { text: "Editar ordem"; visible: !detail.order.completed; enabled: controller.authenticated; onClicked: page.edit(detail.order) }
                    OrderButton { text: detail.order.following ? "Deixar de seguir" : "Seguir"; enabled: controller.authenticated && !controller.savingOrder; onClicked: controller.followOrder(detail.order.id, !detail.order.following) }
                }
                OrderButton {
                    visible: page.admin && detail.visible && !detail.order.started && !detail.order.completed && !detail.order.cancelled_at && page.cancelId !== detail.order.id
                    text: "Cancelar ordem"
                    enabled: controller.authenticated && !controller.savingOrder
                    onClicked: { page.cancelRevision = detail.order.revision; page.cancelId = detail.order.id }
                }
                ColumnLayout {
                    visible: !!page.cancelId && page.cancelId === detail.order.id && !detail.order.cancelled_at
                    Label { Layout.fillWidth: true; text: "Os IDs por fazer ficam disponíveis para outras ordens. A ordem será eliminada 24 horas após o cancelamento."; wrapMode: Text.Wrap }
                    OrderButton { text: "Manter ordem"; enabled: !controller.savingOrder; onClicked: page.cancelId = "" }
                    OrderButton { text: "Confirmar cancelamento"; enabled: controller.authenticated && !controller.savingOrder; onClicked: { controller.cancelOrder(detail.order.id, page.cancelRevision); page.cancelId = "" } }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: detail.columns === 2 ? 540 : -1
                Layout.alignment: Qt.AlignTop
                spacing: 10
                Repeater {
                    model: detail.visible ? detail.order.items : []
                    delegate: Frame {
                        id: taskRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        readonly property var localDraft: {
                            const drafts = controller.localOrderDrafts || {}
                            const unbound = modelData.state === "pending" && modelData.kind !== "preventive"
                                && modelData.drafts.length === 0 ? drafts[modelData.target] : null
                            return drafts[modelData.id] || unbound || null
                        }
                        readonly property bool localRecovery: !!localDraft && localDraft.item === modelData.id &&
                            (modelData.state === "closed" || modelData.state === "invalid" ||
                                (modelData.state === "pending" && !modelData.available) || !!peerDraft) &&
                            localDraft.id !== modelData.report_id &&
                            !modelData.drafts.some(d => d.id === localDraft.id)
                        readonly property var peerDraft: modelData.drafts.find(d => d.owner_id !== page.owner)
                        readonly property bool peerOnly: !!peerDraft && !modelData.drafts.some(d => d.owner_id === page.owner)
                        padding: 12
                        background: Rectangle { color: "white"; border.color: "#c7d3d9"; radius: 9 }
                        contentItem: ColumnLayout {
                            spacing: 6
                            Label { Layout.fillWidth: true; font.bold: true; wrapMode: Text.Wrap; text: (taskRow.index + 1) + ". " + page.taskLabel(taskRow.modelData) }
                            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: {
                                const item = taskRow.modelData
                                if (item.state === "cancelled") return "Cancelada"
                                if (item.state === "invalid") return "Inválida · " + item.note
                                if (item.state === "closed") return item.report_removed ? "Relatório removido" : "Relatório fechado"
                                if (taskRow.peerOnly) return "Em preenchimento · " + (taskRow.peerDraft.owner_name || "outro técnico")
                                if (taskRow.localDraft) return taskRow.localDraft.closed ? "Fechado · por sincronizar" : "Em preenchimento · por sincronizar"
                                return item.drafts.length ? "Em preenchimento" : "Por fazer"
                            } }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                visible: taskRow.localRecovery
                                text: !taskRow.modelData.available && taskRow.modelData.state === "pending"
                                      ? "A visita está indisponível. O rascunho está neste dispositivo."
                                      : "Há uma cópia guardada neste dispositivo."
                            }
                            OrderButton {
                                primary: true
                                Layout.fillWidth: true
                                visible: taskRow.localRecovery
                                enabled: !controller.busy
                                text: taskRow.localDraft && taskRow.localDraft.conflict ? "Consultar cópia local em conflito" : "Recuperar rascunho local"
                                onClicked: {
                                    const rid = controller.prepareOrderReport(detail.order.id, taskRow.modelData.id, taskRow.localDraft.id)
                                    if (rid !== "!") page.openReport(taskRow.modelData.target, rid)
                                }
                            }
                            OrderButton {
                                primary: !taskRow.localRecovery
                                Layout.fillWidth: true
                                visible: (taskRow.modelData.state === "closed" || (!page.admin && taskRow.modelData.state === "pending")) &&
                                         (!taskRow.localRecovery || taskRow.modelData.available || taskRow.modelData.drafts.length > 0)
                                enabled: (detail.order.started || taskRow.modelData.state === "closed") &&
                                    (taskRow.modelData.available || taskRow.modelData.drafts.length > 0) && !controller.busy
                                text: taskRow.modelData.state === "closed" ? (taskRow.localRecovery ? "Consultar relatório do servidor" : "Consultar relatório") :
                                    taskRow.peerOnly ? "Consultar rascunho · " + (taskRow.peerDraft.owner_name || "outro técnico") :
                                    !taskRow.modelData.available ? "Consultar rascunho" : "Abrir intervenção"
                                onClicked: {
                                    const rid = controller.prepareOrderReport(detail.order.id, taskRow.modelData.id)
                                    if (rid !== "!") page.openReport(taskRow.modelData.target, rid)
                                }
                            }
                            Repeater {
                                model: page.admin || taskRow.modelData.state !== "pending" ? taskRow.modelData.drafts : []
                                delegate: OrderButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: (taskRow.modelData.state === "pending" ? "Consultar rascunho · " : "Recuperar rascunho · ") + (modelData.owner_name || modelData.id.slice(0, 8))
                                    enabled: !controller.busy
                                    onClicked: {
                                        const rid = controller.prepareOrderReport(detail.order.id, taskRow.modelData.id, modelData.id)
                                        if (rid !== "!") page.openReport(taskRow.modelData.target, rid)
                                    }
                                }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: width >= 400 ? 2 : 1
                                DirectionButton {
                                    Layout.fillWidth: true
                                    readonly property bool showing: page.expanded === taskRow.modelData.id
                                    text: "Informação do ID"
                                    pointsUp: showing
                                    Accessible.name: showing ? "Ocultar informação do ID" : "Informação do ID"
                                    Accessible.expanded: showing
                                    onClicked: page.expanded = showing ? "" : taskRow.modelData.id
                                }
                                OrderButton { subtle: true; Layout.fillWidth: true; visible: !page.admin && taskRow.modelData.state === "pending" && detail.order.started; enabled: taskRow.modelData.available && controller.authenticated && !controller.savingOrder; text: "Assinalar impedimento"; onClicked: { page.invalidNote = ""; page.invalidId = taskRow.modelData.id; reason.forceActiveFocus(); } }
                            }
                            Label {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: page.expanded === taskRow.modelData.id
                                text: {
                                    const d = taskRow.modelData.details || {}
                                    const labels = {operator:"Operador",address:"Morada",postal_code:"Código postal",region:"Região",manufacturer:"Fabricante",model:"Modelo",serial:"N.º de série",cpe:"CPE",connectors:"Tomadas",power:"Potência",latitude:"Latitude",longitude:"Longitude",energy_supply:"Energia",power_limit:"Limitação"}
                                    const lines = Object.keys(labels).filter(k => d[k]).map(k => labels[k] + " · " + d[k])
                                    if (taskRow.modelData.client) lines.unshift("Cliente · " + taskRow.modelData.client)
                                    if (taskRow.modelData.contract_notes) lines.push("Notas · " + taskRow.modelData.contract_notes)
                                    return lines.join("\n") || "Sem informação adicional."
                                }
                            }
                            TextArea {
                                id: reason
                                Layout.fillWidth: true
                                wrapMode: TextEdit.Wrap
                                visible: page.invalidId === taskRow.modelData.id
                                text: visible ? page.invalidNote : ""
                                placeholderText: "Observação obrigatória"
                                Accessible.name: placeholderText
                                onTextChanged: if (visible && activeFocus) page.invalidNote = text
                            }
                            RowLayout {
                                visible: reason.visible
                                OrderButton { text: "Cancelar"; onClicked: page.invalidId = "" }
                                OrderButton { text: "Marcar inválida"; enabled: taskRow.modelData.available && controller.authenticated && !controller.savingOrder && reason.text.trim().length > 0 && reason.text.length <= 4000; onClicked: controller.invalidateOrderItem(detail.order.id, taskRow.modelData.id, detail.order.revision, reason.text) }
                            }
                        }
                    }
                }
            }
        }
        ColumnLayout {
            visible: page.mode === "edit" && page.admin
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: page.planUnavailable || page.needsPlanReview
                text: page.planUnavailable ? "A ordem foi concluída, cancelada ou deixou de estar disponível." :
                    "A ordem mudou no servidor. Reveja as alterações antes de guardar."
            }
            OrderButton {
                text: "Rever alterações"
                visible: page.needsPlanReview && !page.planUnavailable && !page.planConflicts.length
                enabled: !controller.savingOrder
                onClicked: page.reviewPlan()
            }
            Repeater {
                model: page.planConflicts
                delegate: ColumnLayout {
                    id: conflict
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: conflict.modelData.label + " · valor atual no servidor\n" + conflict.modelData.display
                    }
                    OrderButton { text: "Usar valor do servidor"; onClicked: page.resolvePlanConflict(conflict.modelData, true) }
                    OrderButton { text: "Manter o meu valor"; onClicked: page.resolvePlanConflict(conflict.modelData, false) }
                }
            }
            Label { text: "Início e prazo (AAAA-MM-DD)"; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                TextField { implicitHeight: 44; id: start; Layout.fillWidth: true; Layout.preferredWidth: 125; Accessible.name: "Data de início"; maximumLength: 10 }
                TextField { implicitHeight: 44; id: due; Layout.fillWidth: true; Layout.preferredWidth: 125; Accessible.name: "Prazo"; maximumLength: 10 }
            }
            Label { text: "Técnicos"; font.bold: true }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                readonly property var previous: page.selected && page.plan.revision ? page.selected.technicians.filter(t => t.role && t.role !== "tech") : []
                visible: previous.length > 0
                text: "Atribuição anterior sem perfil de técnico · " + previous.map(t => t.name).join(", ")
            }
            Repeater {
                model: {
                    if (page.mode !== "edit") return []
                    const directory = (controller.users || []).filter(t => t.role === "tech")
                    const assigned = page.selected && page.plan.revision ? page.selected.technicians : []
                    // Keep assigned technicians visible while the directory is incomplete.
                    return directory.concat(assigned.filter(t => (!t.role || t.role === "tech") &&
                        !directory.some(user => user.id === t.id)))
                }
                delegate: CheckBox {
                    implicitHeight: 44
                    required property var modelData
                    Layout.fillWidth: true; text: modelData.name; checked: page.technicians.includes(modelData.id)
                    onClicked: page.technicians = checked ? page.technicians.concat([modelData.id]) : page.technicians.filter(t => t !== modelData.id)
                }
            }
            Label { text: "Sequência de trabalho"; font.bold: true }
            Repeater {
                model: page.targets
                delegate: ColumnLayout {
                    id: planned
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: (planned.index + 1) + ". " + page.taskLabel(planned.modelData) }
                    RowLayout {
                        DirectionButton {
                            pointsUp: true
                            Accessible.name: "Subir · " + planned.modelData.external_id
                            ToolTip.visible: hovered
                            ToolTip.text: Accessible.name
                            enabled: planned.index > 0
                            onClicked: page.move(planned.index, -1)
                        }
                        DirectionButton {
                            Accessible.name: "Descer · " + planned.modelData.external_id
                            ToolTip.visible: hovered
                            ToolTip.text: Accessible.name
                            enabled: planned.index + 1 < page.targets.length
                            onClicked: page.move(planned.index, 1)
                        }
                        OrderButton { text: "Retirar"; enabled: !planned.modelData.state || planned.modelData.state === "pending"; onClicked: page.targets = page.targets.filter(t => t.target !== planned.modelData.target) }
                    }
                }
            }
            TextField {
                implicitHeight: 44
                id: search
                Layout.fillWidth: true
                placeholderText: "Pesquisar ID, referência ou contrato"
                Accessible.name: placeholderText
                onTextChanged: page.searchTerm = text.trim().toLowerCase()
            }
            readonly property var matches: page.mode === "edit" ? page.candidates.filter(c =>
                !page.targets.some(t => t.target === c.target) && page.taskLabel(c).toLowerCase().includes(page.searchTerm)) : []
            id: editor
            Repeater {
                model: editor.matches.slice(0, 40)
                delegate: OrderButton { required property var modelData; Layout.fillWidth: true; text: "+ " + page.taskLabel(modelData); onClicked: page.targets = page.targets.concat([modelData]) }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: editor.matches.length > 40; text: "Refine a pesquisa para ver os restantes IDs." }
            OrderButton {
                text: "Guardar ordem"; Layout.fillWidth: true
                enabled: !page.planUnavailable && !page.needsPlanReview && !page.planConflicts.length &&
                    !controller.savingOrder && controller.authenticated && page.technicians.length && page.targets.length &&
                    /^\d{4}-\d{2}-\d{2}$/.test(start.text) && /^\d{4}-\d{2}-\d{2}$/.test(due.text) && due.text >= start.text
                onClicked: controller.saveOrder({id: page.plan.id, revision: page.plan.revision, start: start.text, due: due.text, technicians: page.technicians, targets: page.targets.map(t => t.target)})
            }
        }
    }
}
