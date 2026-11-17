// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "TestSignature.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QDebug>
#define CHECK(x) do { if (!(x)) {qCritical()<<"FAIL"<<__LINE__<<#x; return false;} } while(false)
struct LemReviewTest {
    static bool run(Lem &c, const QString &root) {
        c.demo();
        auto primary=c.m_chargers[0].toObject(); primary["station_id"]="physical-station";
        c.m_chargers[0]=primary; c.m_catalogues[c.m_contract]=c.m_chargers;
        auto other=primary;other["id"]="another-membership";other["contract_id"]="other-contract";
        c.m_catalogues["other-contract"]=QJsonArray{other};
        c.openReport("demo-charger");
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
int main(int argc,char **argv){QGuiApplication app(argc,argv);app.setApplicationVersion("0.1.55");QTemporaryDir root;Lem c(root.path());return LemReviewTest::run(c,root.path())?0:1;}
