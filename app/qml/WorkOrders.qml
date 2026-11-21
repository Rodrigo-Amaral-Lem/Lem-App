// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page
    component OrderButton: Button {
        id: button
        implicitHeight: Math.max(44, contentItem.implicitHeight + 12)
        leftPadding: 12
        rightPadding: 12
        contentItem: Text {
            text: button.text
            color: button.enabled ? "#20353c" : "#77858a"
            font: button.font
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitWidth: 78
            radius: 6
            color: button.down ? "#d6ecea" : button.enabled ? "white" : "#eef1f2"
            border.color: button.activeFocus ? "#007d83" : "#a2b3b9"
            border.width: button.activeFocus ? 2 : 1
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
    onOwnerChanged: { mode = "list"; selectedId = ""; plan = {}; targets = []; technicians = []; invalidId = ""; expanded = ""; }
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
        technicians = order ? order.technicians.filter(t => !t.role || t.role === "tech").map(t => t.id) : []
        targets = order ? order.items.slice() : []
        start.text = order ? order.start : controller.planningDay
        due.text = order ? order.due : controller.planningDay
        search.text = ""
        mode = "edit"
    }
    readonly property var eligibleTechnicians: technicians.filter(id =>
        (controller.users || []).some(user => user.id === id && user.role === "tech"))
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
            Layout.fillWidth: true
            visible: page.mode === "detail" && !!page.selected
            readonly property var order: page.selected || {items: [], technicians: []}
            id: detail
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: page.selected ? page.result(detail.order) + "\nInício " + detail.order.start + " · Prazo " + detail.order.due + "\n" + detail.order.technicians.map(t => t.name).join(", ") : "" }
            Label { Layout.fillWidth: true; visible: !!detail.order.completed; text: "Fecho · " + page.localTime(detail.order.completed); wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; visible: !!detail.order.cancelled_at; text: "Cancelamento · " + page.localTime(detail.order.cancelled_at); wrapMode: Text.Wrap }
            RowLayout {
                visible: page.admin && !detail.order.completed && !detail.order.cancelled_at
                OrderButton { text: "Editar ordem"; enabled: controller.authenticated; onClicked: page.edit(detail.order) }
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
            Repeater {
                model: detail.visible ? detail.order.items : []
                delegate: ColumnLayout {
                    id: taskRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    readonly property var localDraft: (controller.localOrderDrafts || {})[modelData.id] || null
                    readonly property bool localRecovery: !!localDraft && localDraft.item === modelData.id &&
                        (modelData.state === "closed" || modelData.state === "invalid" || !!peerDraft) && localDraft.id !== modelData.report_id &&
                        !modelData.drafts.some(d => d.id === localDraft.id)
                    readonly property var peerDraft: modelData.drafts.find(d => d.owner_id !== page.owner)
                    readonly property bool peerOnly: !!peerDraft && !modelData.drafts.some(d => d.owner_id === page.owner)
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#bac6ca" }
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
                    OrderButton { text: page.expanded === taskRow.modelData.id ? "Ocultar informação" : "Informação do ID"; onClicked: page.expanded = page.expanded === taskRow.modelData.id ? "" : taskRow.modelData.id }
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
                    OrderButton {
                        visible: taskRow.modelData.state === "closed" || (!page.admin && taskRow.modelData.state === "pending")
                        enabled: (detail.order.started || taskRow.modelData.state === "closed") && taskRow.modelData.available && !controller.busy
                        text: taskRow.modelData.state === "closed" ? "Consultar relatório" : taskRow.peerOnly ? "Consultar rascunho" : "Abrir intervenção"
                        onClicked: {
                            const rid = controller.prepareOrderReport(detail.order.id, taskRow.modelData.id)
                            if (rid !== "!") page.openReport(taskRow.modelData.target, rid)
                        }
                    }
                    OrderButton {
                        visible: taskRow.localRecovery
                        enabled: !controller.busy
                        text: taskRow.localDraft && taskRow.localDraft.conflict ? "Consultar cópia local em conflito" : "Recuperar rascunho local"
                        onClicked: {
                            const rid = controller.prepareOrderReport(detail.order.id, taskRow.modelData.id, taskRow.localDraft.id)
                            if (rid !== "!") page.openReport(taskRow.modelData.target, rid)
                        }
                    }
                    Repeater {
                        model: page.admin || taskRow.modelData.state !== "pending" ? taskRow.modelData.drafts : []
                        delegate: OrderButton {
                            required property var modelData
                            text: (taskRow.modelData.state === "pending" ? "Consultar rascunho · " : "Recuperar rascunho · ") + (modelData.owner_name || modelData.id.slice(0, 8))
                            enabled: !controller.busy
                            onClicked: {
                                const rid = controller.prepareOrderReport(detail.order.id, taskRow.modelData.id, modelData.id)
                                if (rid !== "!") page.openReport(taskRow.modelData.target, rid)
                            }
                        }
                    }
                    OrderButton { visible: !page.admin && taskRow.modelData.state === "pending" && detail.order.started; enabled: controller.authenticated && !controller.savingOrder; text: "Assinalar impedimento"; onClicked: { page.invalidNote = ""; page.invalidId = taskRow.modelData.id; reason.forceActiveFocus(); } }
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
                        OrderButton { text: "Marcar inválida"; enabled: !controller.savingOrder && reason.text.trim().length > 0 && reason.text.length <= 4000; onClicked: controller.invalidateOrderItem(detail.order.id, taskRow.modelData.id, detail.order.revision, reason.text) }
                    }
                }
            }
        }
        ColumnLayout {
            visible: page.mode === "edit" && page.admin
            Layout.fillWidth: true
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
                model: page.mode === "edit" ? (controller.users || []).filter(t => t.role === "tech") : []
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
                        OrderButton { text: "Subir"; enabled: planned.index > 0; onClicked: page.move(planned.index, -1) }
                        OrderButton { text: "Descer"; enabled: planned.index + 1 < page.targets.length; onClicked: page.move(planned.index, 1) }
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
                enabled: !controller.savingOrder && controller.authenticated && page.eligibleTechnicians.length && page.targets.length && /^\d{4}-\d{2}-\d{2}$/.test(start.text) && /^\d{4}-\d{2}-\d{2}$/.test(due.text) && due.text >= start.text
                onClicked: controller.saveOrder({id: page.plan.id, revision: page.plan.revision, start: start.text, due: due.text, technicians: page.eligibleTechnicians, targets: page.targets.map(t => t.target)})
            }
        }
    }
}
