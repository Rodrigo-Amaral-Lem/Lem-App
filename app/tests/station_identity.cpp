#include "Lem.h"
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
        qInfo()<<"PASS: imported identity prefill, failed closure isolation, cross-contract cache updates, offline reopen and server confirmation";
        return true;
    }
};
int main(int argc,char **argv){QGuiApplication app(argc,argv);app.setApplicationVersion("0.1.55");QTemporaryDir root;Lem c(root.path());return LemReviewTest::run(c,root.path())?0:1;}
