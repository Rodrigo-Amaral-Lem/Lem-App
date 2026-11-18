// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#define CHECK(x) do {if(!(x)){qCritical()<<"FAIL"<<__LINE__<<#x;return false;}}while(false)
struct LemReviewTest {
 static bool run(Lem &c,const QString &root) {
  c.demo();c.openReport("demo-charger");
  // Suggestions are generated only on transition; manual edits and clearing are preserved.
  for (auto section : c.m_current["template"].toObject()["sections"].toArray())
   for (auto entry : section.toObject()["items"].toArray()) {
    auto item=entry.toObject(); auto key=item["id"].toString(); auto suggested=item["na_notes_default"].toString();
    if(suggested.isEmpty()) continue;
    auto notes=[&](){return c.m_current["payload"].toObject()["answers"].toObject()[key].toObject()["notes"].toString();};
    c.answer(key,"OK",""); c.answer(key,"NA",""); CHECK(notes()==suggested);
    c.answer(key,"NA",""); CHECK(notes().isEmpty());
    for (const auto &next : QStringList{"OK", "NOK", ""}) {
        c.answer(key,"",""); c.answer(key,"NA",""); CHECK(notes()==suggested);
        c.answer(key,next,suggested); CHECK(notes().isEmpty());
        c.answer(key,"",""); c.answer(key,"NA","Motivo diferente");
        c.answer(key,next,"Motivo diferente"); CHECK(notes()=="Motivo diferente");
    }
    if(item["reading_required"].toBool()) for(const auto &assessment : QStringList{"OK","NOK"}) {
        c.answer(key,"",""); c.answer(key,"NA",""); c.setAssessment(key,assessment); CHECK(notes().isEmpty());
        c.answer(key,"NA","Motivo diferente"); c.setAssessment(key,assessment); CHECK(notes()=="Motivo diferente");
    }

    c.answer(key,"OK","Motivo do tecnico"); c.answer(key,"NA","Motivo do tecnico"); CHECK(notes()=="Motivo do tecnico");
   }
  auto old=c.m_current;auto schema=old["template"].toObject();auto sections=schema["sections"].toArray();
  for(int i=0;i<sections.size();++i){auto section=sections[i].toObject();auto items=section["items"].toArray();
   for(int j=0;j<items.size();++j){auto item=items[j].toObject();if(item["id"]=="MAN.12"||item["id"]=="MAN.18"){item.remove("reading_required");item["photo_required"]=false;item.remove("photo_always");}if(item["id"]=="MAN.19")item["kind"]="text";items[j]=item;}
   section["items"]=items;sections[i]=section;}
  schema["sections"]=sections;old["template"]=schema;c.m_reports[old["id"].toString()]=old;
  c.openReport("demo-charger",old["id"].toString());
  c.setAssessment("MAN.18","OK");
  CHECK(c.m_current["payload"].toObject()["answers"].toObject()["MAN.18"].toObject()["assessment"]=="OK");
  for(auto s:c.sections())for(auto v:s.toMap()["items"].toList())c.answer(v.toMap()["id"].toString(),"NA","Sem equipamento");
  c.answer("EST.04","charger-001 / dispenser-002","");
  c.answer("MAN.12","OK","");c.finish();CHECK(c.m_current["status"]=="draft");
  c.answer("MAN.12","30 mA","");c.finish();CHECK(c.m_current["status"]=="draft");
  QImage evidence(20,20,QImage::Format_RGB32);evidence.fill(Qt::cyan);
  const auto evidencePath=root+"/trip.png";CHECK(evidence.save(evidencePath));
  c.attach("MAN.12",QUrl::fromLocalFile(evidencePath));
  // Photos selected while a draft PUT is in flight must survive its acknowledgement.
  const auto beforeAttachment=c.m_current;
  const auto beforeCount=beforeAttachment["photos"].toArray().size();
  c.m_busy=true;
  c.attachMany("SITE.06",{QUrl::fromLocalFile(evidencePath)});
  CHECK(!c.attachingFiles());
  c.m_busy=false;
  c.m_syncing=true;
  c.m_refreshing=true;
  c.m_downloads.insert("background-photo");
  c.attachMany("SITE.06",{QUrl::fromLocalFile(evidencePath),QUrl::fromLocalFile(evidencePath)});
  CHECK(c.attachingFiles());
  QElapsedTimer attachTimer;attachTimer.start();
  while(c.attachingFiles() && attachTimer.elapsed()<5000) QCoreApplication::processEvents();
  CHECK(!c.attachingFiles());
  CHECK(c.m_current["photos"].toArray().size()==beforeCount+2);
  auto acknowledgement=beforeAttachment;acknowledgement["revision"]=2;
  CHECK(c.mergeSync(acknowledgement,true));
  CHECK(c.m_current["photos"].toArray().size()==beforeCount+2);
  CHECK(c.m_current["dirty"].toBool());
  CHECK(c.m_current["_defer_photo_sync"].toBool());
  for(int i=beforeCount;i<beforeCount+2;++i){
    const auto photo=c.m_current["photos"].toArray()[i].toObject();
    CHECK(photo["checkpoint"]=="SITE.06" && !photo["uploaded"].toBool());
    CHECK(QFileInfo::exists(QUrl(photo["path"].toString()).toLocalFile()));
  }
  c.m_syncing=false;
  c.m_refreshing=false;
  c.m_downloads.clear();
  c.answer("MAN.18","120 Ω","");c.setAssessment("MAN.18","OK");
  c.setAssessment("MAN.18","NOK");
  CHECK(c.m_current["payload"].toObject()["answers"].toObject()["MAN.18"].toObject()["value"]=="120 Ω");
  c.answer("MAN.18","121 Ω","Terra verificada");
  CHECK(c.m_current["payload"].toObject()["answers"].toObject()["MAN.18"].toObject()["assessment"]=="NOK");
  for(int n=14;n<=17;++n){
   auto key=QString("MAN.%1").arg(n);
   c.answer(key,"","");c.setAssessment(key,"OK");
   CHECK(c.m_current["payload"].toObject()["answers"].toObject()[key].toObject()["value"].toString().isEmpty());
   c.finish();CHECK(c.m_current["status"]=="draft");
   c.answer(key,"OK","");c.finish();CHECK(c.m_current["status"]=="draft");
   c.answer(key,"600 MΩ","");
   CHECK(c.m_current["payload"].toObject()["answers"].toObject()[key].toObject()["assessment"]=="OK");
   c.setAssessment(key,"NOK");c.finish();CHECK(c.m_current["status"]=="draft");
   c.answer(key,"450,5 MΩ","Isolamento abaixo do limite");
   CHECK(c.m_current["payload"].toObject()["answers"].toObject()[key].toObject()["assessment"]=="NOK");
   c.answer(key,"NA","");CHECK(!c.m_current["payload"].toObject()["answers"].toObject()[key].toObject().contains("assessment"));
   c.setAssessment(key,"OK");CHECK(c.m_current["payload"].toObject()["answers"].toObject()[key].toObject()["value"]=="");
   c.answer(key,"600","");
  }
  c.answer("MAN.02","Texto de um rascunho antigo","");
  c.finish();CHECK(c.m_current["status"]=="draft");
  c.answer("MAN.02","OK","");
  c.finish();CHECK(c.m_current["status"]=="draft");
  c.answer("FIN.02","Tecnico de teste","");
  CHECK(c.importSignature("FIN.02",QUrl::fromLocalFile(evidencePath)));
  c.answer("FIN.02","Outro tecnico","");
  c.finish();CHECK(c.m_current["status"]=="draft");
  CHECK(c.importSignature("FIN.02",QUrl::fromLocalFile(evidencePath)));
  // Leaving optional notes untouched must not serialize null answers on closure.
  // MAN.14–18 require photographs for both outcomes; a video cannot satisfy it.
  for(int n=14;n<=18;++n){
   const auto key=QString("MAN.%1").arg(n);
   c.finish();CHECK(c.m_current["status"]=="draft");
   c.attach(key,QUrl::fromLocalFile(evidencePath));
  }
  auto optionalPayload=c.m_current["payload"].toObject();
  auto optionalAnswers=optionalPayload["answers"].toObject();
  for (const auto &key : {"SEG.07","SITE.06","QVE.14","EST.18","MAN.22","CARG.07","FIN.03"}) optionalAnswers.remove(key);
  optionalPayload["answers"]=optionalAnswers;c.m_current["payload"]=optionalPayload;
  c.finish();CHECK(c.m_current["status"]=="closed");
  for (auto answer:c.m_current["payload"].toObject()["answers"].toObject()) CHECK(answer.isObject());
  CHECK(c.pendingCount()>0);
  auto legacy=c.m_current;auto legacyPayload=legacy["payload"].toObject();
  auto legacyAnswers=legacyPayload["answers"].toObject();
  legacyAnswers["SITE.06"]=QJsonValue::Null;
  legacyAnswers["SEG.01"]=QJsonValue::Null;
  legacyPayload["answers"]=legacyAnswers;legacy["payload"]=legacyPayload;legacy["_inflight_payload"]=legacyPayload;
  const auto repaired=Checklist::withoutNullOptionalAnswers(legacy);
  CHECK(!repaired["payload"].toObject()["answers"].toObject().contains("SITE.06"));
  CHECK(repaired["payload"].toObject()["answers"].toObject().value("SEG.01").isNull());
  CHECK(repaired["payload"].toObject()["signatures"]==legacyPayload["signatures"]);
  CHECK(repaired["_inflight_payload"]==repaired["payload"]);
  CHECK(repaired["photos"]==legacy["photos"] && repaired["status"]=="closed");
  CHECK(Checklist::withoutNullOptionalAnswers(repaired)==repaired);
  const auto id=c.m_current["id"].toString();
  // A closed report awaiting acknowledgement must keep all private evidence.
  auto report=c.m_current;report["revision"]=3;report["dirty"]=true;
  QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::cyan);
  QDir().mkpath(c.cacheDir()+"/photos");const auto path=c.cacheDir()+"/photos/cleanup.png";CHECK(image.save(path));
  report["photos"]=QJsonArray{QJsonObject{{"id","photo"},{"checkpoint","SITE.02"},{"uploaded",true},{"mime","image/png"},{"path",QUrl::fromLocalFile(path).toString()}}};
  c.m_current=report;c.m_reports[id]=report;c.clearLocalCopies();CHECK(QFileInfo::exists(path));
  report["dirty"]=false;report["deleted_photos"]=QJsonArray{};report["offline_photos"]=true;c.m_current=report;c.m_reports[id]=report;c.remember();
  c.clearLocalCopies();CHECK(!QFileInfo::exists(path));CHECK(c.m_current["id"]==id);
  CHECK(c.m_current["payload"]==report["payload"]);CHECK(c.m_current["status"]=="closed");
  CHECK(c.m_current["photos"].toArray()[0].toObject()["path"].toString().isEmpty());
  Lem restored(root);restored.demo();restored.openReport("",id);
  CHECK(restored.m_current["id"]==id && restored.m_current["payload"]==report["payload"]);
  CHECK(restored.m_current["photos"].toArray()[0].toObject()["path"].toString().isEmpty());
  qInfo()<<"PASS: independent mandatory readings, NOK notes, NA reset, optional serial photo, closed/pending cleanup and restart";return true;
 }
};
int main(int argc,char**argv){QGuiApplication app(argc,argv);app.setApplicationVersion("0.3.2");QTemporaryDir dir;Lem c(dir.path());return LemReviewTest::run(c,dir.path())?0:1;}
