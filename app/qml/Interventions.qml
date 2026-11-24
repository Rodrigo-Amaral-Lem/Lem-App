// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    id: page
    required property var controller
    signal openReport(string target, string report)
    signal manageIds(string contractId, string name)
    signal deleteJob(string jobId, string reference)
    signal recoverJobs(string kind)
    signal openOrder(string id)
    signal openNotes(string id, string externalId)
    property string kind: "corrective"
    property string mode: "list"
    property string selectedId: ""
    property var station: null
    property var coverage: []
    property bool coverageReady: false
    property string requestId: ""
    property bool saving: false
    property string message: ""
    property url workbook
    readonly property string owner: controller.user.id || ""
    readonly property bool admin: controller.user.role === "admin"
    readonly property bool corrective: kind === "corrective"
    readonly property var jobs: (controller.interventions || []).filter(j => j.kind === kind)
                                 .sort((a, b) => b.reference.localeCompare(a.reference))
    readonly property var job: jobs.find(j => j.id === selectedId) || null
    readonly property var team: [{id: "", name: "Sem atribuição individual"}].concat(
        (controller.users || []).filter(u => u.role === "tech"))
    readonly property var order: orderForJob(job)
    readonly property string heading: mode === "contracts" ? "Contratos de corretivas" :
        mode === "import" ? "Importar contrato de corretivas" : mode === "pick" ? "Escolher PCVE" :
        mode === "create" ? (corrective ? "Nova corretiva" : "Novo comissionamento") :
        mode === "detail" && job ? job.reference : (corrective ? "Manutenções corretivas" : "Comissionamentos")
    clip: true
    contentWidth: availableWidth
    onModeChanged: {
        contentItem.contentY = 0
        if (mode === "detail" && admin && controller.authenticated) controller.loadUsers()
    }
    onJobChanged: {
        if (mode === "detail" && !job) mode = "list"
    }
    onOwnerChanged: { saving = false; requestId = ""; mode = "list"; station = null; coverage = []; message = ""; selectedId = "" }

    function orderForJob(currentJob) {
        if (!currentJob) return null
        const orders = (controller.workOrders || []).filter(order => !order.cancelled_at)
        const belongs = item => item.target === currentJob.id
        const pending = orders.find(order => order.items.some(item => belongs(item) && item.state === "pending"))
        if (pending || !currentJob.order_only) return pending || null
        const result = orders.find(order => order.items.some(item => belongs(item) && item.state === "closed"
            && item.report_id === currentJob.report_id))
        return result || orders.find(order => order.items.some(item => belongs(item) && item.state !== "cancelled")) || null
    }

    function list(type) {
        kind = type
        selectedId = ""
        mode = "list"
        message = ""
        filter.text = ""
        if (controller.authenticated) controller.refresh()
    }
    function back() {
        if (saving) return
        message = ""
        mode = mode === "import" ? "contracts" : "list"
        if (mode === "contracts") controller.loadCorrectiveContracts("")
    }
    function begin(row) {
        station = row
        mode = "create"
        requestId = controller.newOrderId()
        coverageReady = !corrective
        coverage = []
        notes.clear()
        assignee.currentIndex = 0
        message = ""
        if (admin) controller.loadUsers()
        if (corrective) controller.loadCorrectiveContracts(row.id)
    }
    function startImport() {
        mode = "import"
        requestId = controller.newOrderId()
        company.clear()
        contractName.clear()
        start.text = controller.planningDay || Qt.formatDate(new Date(), "yyyy-MM-dd")
        end.text = start.text
        workbook = ""
        message = ""
    }
    function submit() {
        const body = {kind: kind, station_id: station.id}
        if (admin) {
            body.technician = (team[assignee.currentIndex] || {}).id || null
            if (corrective) {
                body.notes = notes.text
                body.contract_id = (coverage[contractChoice.currentIndex] || {}).id || null
            }
        }
        saving = true
        controller.createIntervention(requestId, body)
    }
    function dateRange(c) { return c.start + " a " + c.end }
    Connections {
        target: page.controller
        ignoreUnknownSignals: true
        function onCorrectiveContractsReceived(sid, contracts, error) {
            if ((page.mode === "create" && page.station && sid === page.station.id) ||
                (page.mode === "contracts" && !sid)) {
                page.coverage = contracts
                page.coverageReady = !error
                if (error) page.message = error
                contractChoice.currentIndex = 0
            }
        }
        function onInterventionSaved(id, ok, text) {
            if (id !== page.requestId) return
            page.saving = false
            page.message = text
            if (!ok) return
            if (page.mode === "import") {
                page.mode = "contracts"
                page.controller.loadCorrectiveContracts("")
            } else if (page.mode === "create") {
                page.selectedId = id
                page.mode = "detail"
            }
        }
    }
    FileDialog {
        id: picker
        title: "Selecionar a folha de IDs"
        nameFilters: ["Folhas Excel (*.xlsx)"]
        onAccepted: page.workbook = selectedFile
    }
    component Line: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
    component Action: Button {
        id: action
        Layout.fillWidth: true
        implicitHeight: Math.max(44, contentItem.implicitHeight + 14)
        contentItem: Text {
            text: action.text
            font: action.font
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: action.enabled ? "#263238" : "#65737a"
            wrapMode: Text.Wrap
        }
    }
    component Entry: TextField {
        Layout.fillWidth: true
        implicitHeight: 44
        Accessible.name: placeholderText
    }
    ColumnLayout {
        width: page.availableWidth
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Line { text: page.heading; font.pixelSize: 22; font.bold: true }
            Button { text: "Voltar"; implicitHeight: 44; visible: page.mode !== "list"; enabled: !page.saving; onClicked: page.back() }
        }
        Line { visible: !!page.message; text: page.message; color: "#245162" }
        ColumnLayout {
            visible: page.mode === "list"
            Layout.fillWidth: true
            Action {
                visible: page.admin || !page.corrective
                enabled: page.controller.authenticated && !page.saving
                text: page.corrective ? "Nova corretiva" : "Novo comissionamento"
                onClicked: { page.mode = "pick"; stationSearch.clear(); page.message = ""; page.controller.refreshStations() }
            }
            Action {
                visible: page.admin && page.corrective
                enabled: page.controller.authenticated
                text: "Contratos de corretivas"
                onClicked: { page.mode = "contracts"; page.coverage = []; page.message = ""; page.controller.loadCorrectiveContracts("") }
            }
            Action {
                visible: page.admin
                enabled: page.controller.authenticated && !page.controller.busy
                text: "Recuperar pedidos apagados"
                onClicked: page.recoverJobs(page.kind)
            }
            Entry { id: filter; placeholderText: "Pesquisar ID, referência ou contrato" }
            readonly property var matches: page.jobs.filter(j =>
                (j.external_id + " " + j.reference + " " + (j.contract || {}).name).toLowerCase().includes(filter.text.trim().toLowerCase()))
            id: listing
            Line { visible: !listing.matches.length; text: page.corrective ? "Sem corretivas para mostrar." : "Sem comissionamentos para mostrar." }
            Repeater {
                model: page.mode === "list" ? listing.matches.slice(0, 80) : []
                Action {
                    required property var modelData
                    text: modelData.reference + " · " + modelData.external_id + "\n" +
                          (modelData.status === "closed" ? "Relatório fechado" : modelData.report_id ? "Rascunho" : "Por fazer")
                          + (modelData.contract ? " · " + modelData.contract.name : "")
                    onClicked: { page.selectedId = modelData.id; page.mode = "detail"; page.message = ""; if (page.admin && page.controller.authenticated) page.controller.loadUsers() }
                }
            }
            Line { visible: listing.matches.length > 80; text: "Refine a pesquisa para ver as restantes intervenções." }
            Action { text: "Atualizar"; enabled: page.controller.authenticated; onClicked: page.controller.refresh() }
        }
        ColumnLayout {
            visible: page.mode === "pick"
            Layout.fillWidth: true
            Entry { id: stationSearch; placeholderText: "Pesquisar ID de PCVE" }
            readonly property var matches: (page.controller.stations || []).filter(s =>
                s.external_id.toLowerCase().includes(stationSearch.text.trim().toLowerCase()))
            id: stations
            Line { visible: !stations.matches.length; text: page.controller.stationCatalogueMessage || "Sem IDs disponíveis." }
            Repeater {
                model: page.mode === "pick" ? stations.matches.slice(0, 40) : []
                Action { required property var modelData; text: modelData.external_id; onClicked: page.begin(modelData) }
            }
            Line { visible: stations.matches.length > 40; text: "Refine a pesquisa para ver os restantes IDs." }
        }
        ColumnLayout {
            visible: page.mode === "create"
            Layout.fillWidth: true
            Line { text: (page.station || {}).external_id || ""; font.bold: true }
            Line { visible: page.corrective; text: "Contrato aplicável" }
            ComboBox {
                id: contractChoice
                Layout.fillWidth: true
                implicitHeight: 44
                visible: page.corrective
                enabled: page.coverageReady && !page.saving
                Accessible.name: "Contrato aplicável"
                model: page.coverage.map(c => c.name).concat(["Sem contrato"])
            }
            Line { visible: page.corrective && !!page.coverage[contractChoice.currentIndex]; text: visible ? page.dateRange(page.coverage[contractChoice.currentIndex]) : "" }
            Action { text: "Recarregar contratos"; visible: page.corrective && !page.coverageReady; onClicked: page.controller.loadCorrectiveContracts(page.station.id) }
            Line { visible: page.admin; text: "Atribuição individual" }
            ComboBox { id: assignee; visible: page.admin; implicitHeight: 44; Layout.fillWidth: true; model: page.team; textRole: "name"; Accessible.name: "Técnico" }
            Line { visible: page.corrective; text: "Notas para o técnico" }
            TextArea { id: notes; visible: page.corrective; Layout.fillWidth: true; placeholderText: "Instruções para esta corretiva"; wrapMode: TextEdit.Wrap; selectByMouse: true }
            Action {
                text: page.corrective ? "Criar corretiva" : "Criar comissionamento"
                enabled: page.coverageReady && !page.saving && page.controller.authenticated && notes.length <= 32767
                onClicked: page.submit()
            }
        }
        ColumnLayout {
            visible: page.mode === "detail" && !!page.job
            Layout.fillWidth: true
            Line { text: (page.job || {}).external_id || ""; font.bold: true }
            Line { visible: page.corrective; text: ((page.job || {}).contract || {}).name || "Sem contrato" }
            Line { text: (page.job || {}).notes || ""; visible: !!text }
            Action {
                text: page.order ? "Abrir ordem " + page.order.code : (page.job || {}).report_id ? "Abrir relatório" : "Abrir intervenção"
                enabled: !page.controller.busy && !page.saving && (page.order || !(page.job || {}).order_only)
                onClicked: {
                    if (page.order) { page.openOrder(page.order.id); return }
                    const report = page.controller.prepareIntervention(page.selectedId)
                    if (report !== "!") page.openReport(page.selectedId, report)
                }
            }
            Action {
                visible: page.corrective && page.admin
                enabled: !page.controller.busy && !page.saving
                text: "Notas da corretiva"
                onClicked: { page.controller.selectContract(page.selectedId); page.openNotes(page.selectedId, page.job.external_id) }
            }
            Action {
                visible: page.admin && !!page.job && page.job.status !== "closed"
                enabled: page.controller.authenticated && !page.controller.busy
                text: "Apagar pedido"
                onClicked: page.deleteJob(page.job.id, page.job.reference)
            }
            Line { visible: page.admin; text: "Atribuição individual" }
            ComboBox {
                id: share
                visible: page.admin
                enabled: !page.controller.busy && !page.saving
                implicitHeight: 44
                Layout.fillWidth: true
                model: page.team
                textRole: "name"
                Accessible.name: "Partilhar com técnico"
                currentIndex: Math.max(0, page.team.findIndex(u => u.id === ((page.job || {}).technician || "")))
            }
            Line {
                visible: changeAssignee.movesDraft
                text: "O rascunho e as evidências guardadas no servidor passam para o técnico selecionado."
            }
            Action {
                id: changeAssignee
                readonly property var technician: page.team[share.currentIndex] || null
                readonly property bool changesTechnician: !!page.job && !!technician &&
                    technician.id !== (page.job.technician || "")
                readonly property bool movesDraft: page.admin && changesTechnician && !!technician.id && !!page.job.handoff
                visible: page.admin
                text: movesDraft ? "Transferir atribuição e rascunho" : "Guardar atribuição"
                enabled: page.controller.authenticated && !page.controller.busy && !page.saving && changesTechnician
                onClicked: {
                    const body = {technician: technician.id || null, previous: page.job.technician || null}
                    if (movesDraft) body.handoff = page.job.handoff
                    page.saving = true
                    page.requestId = page.selectedId
                    page.controller.shareIntervention(page.selectedId, body)
                }
            }
        }
        ColumnLayout {
            visible: page.mode === "contracts"
            Layout.fillWidth: true
            Action { text: "Importar contrato"; onClicked: page.startImport() }
            Line { visible: !page.coverage.length; text: "Sem contratos para mostrar." }
            Repeater {
                model: page.mode === "contracts" ? page.coverage : []
                Action {
                    required property var modelData
                    text: modelData.name + "\n" + page.dateRange(modelData) + " · Gerir IDs"
                    onClicked: page.manageIds(modelData.id, modelData.name)
                }
            }
        }
        ColumnLayout {
            visible: page.mode === "import"
            Layout.fillWidth: true
            Entry { id: company; placeholderText: "Cliente"; maximumLength: 120 }
            Entry { id: contractName; placeholderText: "Nome do contrato"; maximumLength: 115 }
            Line { text: "Validade (AAAA-MM-DD)" }
            Entry { id: start; placeholderText: "Data de início"; maximumLength: 10 }
            Entry { id: end; placeholderText: "Data de fim"; maximumLength: 10 }
            Line { text: "Na primeira folha do Excel, coloque um ID de PCVE por linha na coluna A, sem cabeçalho." }
            Action { text: "Selecionar Excel"; onClicked: picker.open() }
            Line { visible: !!page.workbook.toString(); text: page.workbook.toString().split("/").pop() }
            Action {
                text: "Criar contrato"
                enabled: page.controller.authenticated && !page.saving && !!company.text.trim() && !!contractName.text.trim() &&
                         /^\d{4}-\d{2}-\d{2}$/.test(start.text) && /^\d{4}-\d{2}-\d{2}$/.test(end.text) && !!page.workbook.toString()
                onClicked: {
                    page.saving = true
                    page.controller.importCorrectiveContract(page.requestId, {company: company.text, name: contractName.text, start: start.text, end: end.text}, page.workbook)
                }
            }
        }
    }
}
