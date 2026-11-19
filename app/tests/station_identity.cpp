// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "TestSignature.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QDebug>
#include <QElapsedTimer>
#include <QSettings>
#include <QThread>
#include <utility>
#define CHECK(x) do { if (!(x)) {qCritical()<<"FAIL"<<__LINE__<<#x; return false;} } while(false)
struct LemReviewTest {
    static bool cachedPcveFields() {
        const QJsonObject legacy{{"address", "CALIÝOS, EstaÝão"}, {"serial", "Ý001"},
                                 {"location", "Local antigo"}, {"site_id", "Outro"},
                                 {"extra.Nome do local", "Legado"}, {"source", QJsonObject{{"Morada", "Antiga"}}}};
        const QJsonObject row{{"external_id", "ID-Ý01"}, {"details", legacy}};
        const auto cleaned = Lem::currentPcveRows(QJsonArray{row}).at(0).toObject();
        const auto details = cleaned["details"].toObject();
        CHECK(cleaned["external_id"] == "ID-Ý01" && details["serial"] == "Ý001");
        CHECK(details["address"] == "CALIÇOS, Estação" && details.size() == 2);
        return true;
    }

    static bool waitUntil(const std::function<bool()> &done) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!done() && elapsed.elapsed() < 8000) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        return done();
    }

    static QJsonObject station(const Lem &c, const QString &externalId) {
        for (const auto &value : c.m_stations) {
            const auto row = value.toObject();
            if (row["external_id"] == externalId) return row;
        }
        return {};
    }

    static QJsonObject membership(const Lem &c, const QString &contract, const QString &externalId) {
        for (const auto &value : c.m_catalogues[contract].toArray()) {
            const auto row = value.toObject();
            if (row["external_id"] == externalId) return row;
        }
        return {};
    }

    // Start from the device cache and close before the first upload. Another client
    // imports the same station in the meantime, while the original answer stays untouched.
    static bool closedOffline(Lem &manager, const QString &server, const QString &file,
                              const QString &contract, const QString &externalId) {
        QTemporaryDir root;
        Lem field(root.path());
        field.setServer(server);
        field.login("pcve-admin@example.invalid", qEnvironmentVariable("LEM_TEST_CATALOGUE_PASSWORD"));
        CHECK(waitUntil([&] { return field.authenticated() && !field.refreshing(); }));
        field.selectContract(contract);
        CHECK(waitUntil([&] { return !field.refreshing() && !membership(field, contract, externalId).isEmpty(); }));
        const auto charger = membership(field, contract, externalId);
        const auto original = charger["details"].toObject()["model"].toString();
        const auto token = std::exchange(field.m_token, QString{});
        CHECK(field.openReport(charger["id"].toString()));
        for (const auto &section : field.sections())
            for (const auto &item : section.toMap()["items"].toList()) {
                const auto key = item.toMap()["id"].toString();
                if (key != "EST.03")
                    field.answer(key, "NA", "Sem ensaio");
            }
        CHECK(addTestSignature(field));
        field.finish();
        CHECK(field.m_current["status"] == "closed" && field.m_current["revision"] == 0);

        manager.analyseStationExcel(QUrl::fromLocalFile(file));
        CHECK(waitUntil([&] { return !manager.busy() && manager.m_importInfo["station_catalogue"].toBool(); }));
        manager.previewStationExcel("Manual", 1, {{"external_id", "ID"}, {"model", "Modelo"}});
        CHECK(waitUntil([&] { return !manager.busy() && manager.m_preview["count"] == 2; }));
        manager.commitExcel();
        CHECK(waitUntil([&] { return !manager.busy() && station(manager, externalId)["details"].toObject()["model"] == "Imported model"; }));
        CHECK(original != "Imported model");
        field.m_token = token;
        field.sync();
        CHECK(waitUntil([&] { return !field.syncing() && !field.m_current["dirty"].toBool(); }));
        CHECK(field.m_current["status"] == "closed" && field.m_current["revision"].toInt() >= 2);
        CHECK(field.m_current["snapshot"].toObject()["charger"].toObject()["model"] == original);
        manager.refreshStations();
        CHECK(waitUntil([&] { return !manager.stationCatalogueLoading(); }));
        CHECK(station(manager, externalId)["details"].toObject()["model"] == "Imported model");
        qInfo() << "PASS: report closed offline kept its opening model, and first sync preserved the later Excel update";
        return true;
    }

    // Run against the disposable API fixture with real native HTTP and Excel.
    static bool live(Lem &c, const QString &server, const QString &file) {
        const QString shared = "PCVE-DEMO-001";
        const QString created = "PCVE-DESKTOP-NEW-901";
        c.setServer(server);
        c.login("pcve-admin@example.invalid", qEnvironmentVariable("LEM_TEST_CATALOGUE_PASSWORD"));
        CHECK(waitUntil([&] { return c.authenticated() && !c.refreshing() && c.m_contracts.size() == 5; }));
        CHECK(c.canManageStations() && c.contractId().isEmpty());
        c.refreshStations();
        CHECK(waitUntil([&] { return c.stationCatalogueLoaded() && !c.stationCatalogueLoading(); }));
        auto central = station(c, shared);
        CHECK(!central.isEmpty() && !station(c, "PCVE-SEM-CONTRATO-900").isEmpty());

        QString first, second;
        for (const auto &value : std::as_const(c.m_contracts)) {
            const auto contract = value.toObject();
            if (contract["company"] == "Exemplo" && contract["year"] == 2026) {
                if (contract["semester"] == 1) first = contract["id"].toString();
                if (contract["semester"] == 2) second = contract["id"].toString();
            }
        }
        CHECK(!first.isEmpty() && !second.isEmpty());
        c.selectContract(first);
        CHECK(waitUntil([&] { return !c.refreshing() && !membership(c, first, shared).isEmpty(); }));
        c.selectContract(second);
        CHECK(waitUntil([&] { return !c.refreshing() && !membership(c, second, shared).isEmpty(); }));
        const auto firstMembership = membership(c, first, shared);
        const auto secondMembership = membership(c, second, shared);
        CHECK(firstMembership["station_id"] == central["id"] && secondMembership["station_id"] == central["id"]);
        CHECK(firstMembership["id"] != secondMembership["id"]);
        CHECK(c.openReport(secondMembership["id"].toString(), ""));
        const auto snapshot = c.m_current.value("snapshot");

        c.analyseStationExcel(QUrl::fromLocalFile(file));
        CHECK(waitUntil([&] { return !c.busy() && c.m_importInfo["station_catalogue"].toBool(); }));
        c.previewStationExcel("Manual", 1, {{"external_id", "ID"}, {"model", "Modelo"}});
        CHECK(waitUntil([&] { return !c.busy() && c.m_preview["count"] == 2; }));
        CHECK(c.m_preview["errors"].toArray().isEmpty());
        CHECK(station(c, created).isEmpty());
        c.commitExcel();
        CHECK(waitUntil([&] { return !c.busy() && !station(c, created).isEmpty(); }));
        CHECK(station(c, shared)["details"].toObject()["model"] == "Imported model");
        CHECK(membership(c, first, shared)["details"].toObject()["model"] == "Imported model");
        CHECK(membership(c, second, shared)["details"].toObject()["model"] == "Imported model");
        CHECK(c.m_current["snapshot"] == snapshot);

        const auto id = central["id"].toString();
        c.saveStation(id, central["updated"].toString(), {{"model", "Conflicting edit"}});
        CHECK(waitUntil([&] { return c.m_stationEditResult["state"] == "error" && !c.stationCatalogueLoading(); }));
        central = station(c, shared);
        c.saveStation(id, central["updated"].toString(), {{"model", "Edited model"}, {"serial", "000123"}});
        CHECK(waitUntil([&] { return c.m_stationEditResult["state"] == "saved"; }));
        const auto updated = station(c, shared);
        CHECK(updated["details"].toObject()["model"] == "Edited model");
        CHECK(updated["details"].toObject()["serial"] == "000123");
        CHECK(membership(c, first, shared)["details"] == updated["details"]);
        CHECK(membership(c, second, shared)["details"] == updated["details"]);
        CHECK(c.m_current["snapshot"] == snapshot);
        CHECK(membership(c, first, created).isEmpty() && membership(c, second, created).isEmpty());

        // A response dispatched before the edit cannot replace the new revision.
        c.applyStationRecords(QJsonArray{central});
        CHECK(station(c, shared) == updated);
        CHECK(membership(c, first, shared)["details"] == updated["details"]);
        CHECK(c.persist());
        Lem restored(c.storage());
        CHECK(restored.stationCatalogueLoaded() && !restored.authenticated());
        CHECK(station(restored, shared) == updated && !station(restored, created).isEmpty());
        CHECK(membership(restored, second, shared)["details"] == updated["details"]);
        // Simulate a second session deleting the selected row between listing
        // and opening it. The still-cached previous draft must keep its answers.
        const auto removed = qEnvironmentVariable("LEM_TEST_REMOVED_REPORT");
        if (!removed.isEmpty()) {
            const auto previousId = c.m_current["id"].toString();
            const auto original = c.m_reports.value(previousId).toObject().value("payload");
            CHECK(c.openReport({}, removed));
            CHECK(c.m_current.isEmpty());
            CHECK(waitUntil([&] { return !c.busy(); }));
            CHECK(c.message().contains("já não está disponível") && c.m_current.isEmpty());
            c.answer("SEG.01", "OK", {});
            CHECK(c.m_reports[previousId].toObject()["payload"] == original);
            qInfo() << "PASS: selected deleted report never became editable and previous answers were preserved";
        }
        qInfo() << "PASS: desktop Excel and edits over HTTP, canonical cache across contracts, stale conflict and offline checkpoint";
        return closedOffline(c, server, file, second, shared);
    }

    static bool run(Lem &c, const QString &root) {
        c.demo();
        const auto legacyReportId = c.m_current["id"].toString();
        auto primary=c.m_chargers[0].toObject(); primary["station_id"]="physical-station";
        c.m_chargers[0]=primary; c.m_catalogues[c.m_contract]=c.m_chargers;
        auto other=primary;other["id"]="another-membership";other["contract_id"]="other-contract";
        c.m_catalogues["other-contract"]=QJsonArray{other};
        c.openReport("demo-charger");
        c.loadStationReports("physical-station");
        const auto localHistory = c.m_stationReports["rows"].toArray();
        QSet<QString> reportIds;
        for (const auto &entry : localHistory) reportIds.insert(entry.toObject()["id"].toString());
        // demo() created the first report before the fixture gained a station UUID.
        CHECK(localHistory.size() == 2 && reportIds.contains(legacyReportId) &&
              reportIds.contains(c.m_current["id"].toString()));
        c.loadStationReports("unrelated-station");
        CHECK(c.m_stationReports["rows"].toArray().isEmpty());
        auto a=c.m_current["payload"].toObject()["answers"].toObject();
        CHECK(a["EST.02"].toObject()["value"]=="Exemplo");
        CHECK(a["EST.03"].toObject()["value"]=="DC 60 kW");
        CHECK(a["EST.04"].toObject()["value"]=="000012");
        c.answer("EST.02","Corrected maker","");
        CHECK(c.m_chargers[0].toObject()["details"].toObject()["manufacturer"]=="Exemplo");
        for(auto section:c.sections())
            for(auto item:section.toMap()["items"].toList())c.answer(item.toMap()["id"].toString(),"NA","Sem conector/veículo disponível");
        c.answer("EST.02","Corrected maker","");c.answer("EST.03","Full model X","");c.answer("EST.04","SN-UPDATED","");
        CHECK(addTestSignature(c));
        // Imported identity may be absent or cleared while drafting, but not at closure.
        // A catalogue value must not silently replace an empty answer in the report.
        for (const auto &key : {QString("EST.03"), QString("EST.04")}) {
            const auto value = c.m_current["payload"].toObject()["answers"].toObject()[key].toObject()["value"].toString();
            for (const auto &missing : {QString(), QString("   ")}) {
                c.answer(key, missing, "");
                c.finish();
                CHECK(c.m_current["status"] == "draft");
                CHECK(c.message().contains(key + ": falta resposta"));
                CHECK(c.m_chargers[0].toObject()["details"].toObject()["model"] == "DC 60 kW");
            }
            c.answer(key, value, "");
        }
        c.finish();CHECK(c.m_current["status"]=="closed"); // serial photo is optional
        for(auto rows:{c.m_chargers,c.m_catalogues["other-contract"].toArray()}) {
            const auto d=rows[0].toObject()["details"].toObject();
            CHECK(d["manufacturer"]=="Corrected maker" && d["model"]=="Full model X" && d["serial"]=="SN-UPDATED");
        }
        CHECK(c.m_current["snapshot"].toObject()["charger"].toObject()["model"]=="Full model X");
        c.openReport("demo-charger");
        CHECK(c.m_current["payload"].toObject()["answers"].toObject()["EST.03"].toObject()["value"]=="Full model X");
        CHECK(c.persist());
        Lem restored(root);restored.m_server=c.m_server;restored.m_user=c.m_user;restored.loadUser();
        CHECK(restored.m_catalogues["other-contract"].toArray()[0].toObject()["details"].toObject()["serial"]=="SN-UPDATED");
        // A server-confirmed canonical value replaces optimistic cached projections.
        auto ack=c.m_current;ack["status"]="closed";
        ack["station_update"]=QJsonObject{{"station_id","physical-station"},{"external_id",primary["external_id"]},
            {"details",QJsonObject{{"manufacturer","Server maker"},{"model","Server model"},{"serial","Server serial"}}}};
        c.applyClosedIdentity(ack);
        CHECK(c.m_chargers[0].toObject()["details"].toObject()["model"]=="Server model");
        // Private corrections do not project their staged answers into catalogues.
        // Only a committed acknowledgement carries live canonical station data;
        // this also works for legacy templates and for every cached membership.
        auto corrected = c.m_current;
        corrected["status"] = "closed";
        corrected["correction"] = QJsonObject{{"state", "editing"}};
        corrected["payload"] = QJsonObject{{"answers", QJsonObject{
            {"EST.03", QJsonObject{{"value", "Uncommitted model"}}}}}};
        CHECK(c.mergeSync(corrected, false));
        CHECK(c.m_chargers[0].toObject()["details"].toObject()["model"] == "Server model");
        corrected["correction"] = QJsonObject{{"state", "committed"}};
        corrected["template"] = QJsonObject{{"version", 3}};
        // Older servers supply no station_update: never guess from the snapshot.
        CHECK(c.mergeSync(corrected, true));
        CHECK(c.m_chargers[0].toObject()["details"].toObject()["model"] == "Server model");
        auto live = ack["station_update"].toObject();
        auto canonical = live["details"].toObject(); canonical["model"] = "Committed model";
        live["details"] = canonical; corrected["station_update"] = live;
        CHECK(c.mergeSync(corrected, true));
        for (auto rows : {c.m_chargers, c.m_catalogues["other-contract"].toArray()})
            CHECK(rows[0].toObject()["details"].toObject()["model"] == "Committed model");
        qInfo()<<"PASS: imported identity prefill, failed closure isolation, cross-contract cache updates, offline reopen and server confirmation";
        return true;
    }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    const auto args = app.arguments();
    const bool live = args.size() == 4 && args[1] == "--live-catalogue";
    app.setApplicationVersion(live ? "0.5.0" : "0.1.55");
    if (live) {
        app.setOrganizationName("LemStationTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    }
    if (!LemReviewTest::cachedPcveFields()) return 1;
    Lem controller(root.path());
    const bool passed = live ? LemReviewTest::live(controller, args[2], args[3])
                             : LemReviewTest::run(controller, root.path());
    return passed ? 0 : 1;
}
