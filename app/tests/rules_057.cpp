// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#define CHECK(x) do {if(!(x)){qCritical()<<"FAIL"<<__LINE__<<#x;return false;}}while(false)
struct LemReviewTest {
 static bool run(Lem &c,const QString &root) {
  c.demo();c.openReport("demo-charger");
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
  c.finish();CHECK(c.m_current["status"]=="closed");
  const auto id=c.m_current["id"].toString();
  // A closed report awaiting acknowledgement must keep all private evidence.
  auto report=c.m_current;report["revision"]=3;report["dirty"]=true;
  QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::cyan);
  QDir().mkpath(c.cacheDir()+"/photos");const auto path=c.cacheDir()+"/photos/cleanup.png";CHECK(image.save(path));
  report["photos"]=QJsonArray{QJsonObject{{"id","photo"},{"checkpoint","SITE.02"},{"uploaded",true},{"mime","image/png"},{"path",QUrl::fromLocalFile(path).toString()}}};
  c.m_current=report;c.m_reports[id]=report;c.clearLocalCopies();CHECK(QFileInfo::exists(path));
  report["dirty"]=false;report["offline_photos"]=true;c.m_current=report;c.m_reports[id]=report;c.remember();
  c.clearLocalCopies();CHECK(!QFileInfo::exists(path));CHECK(c.m_current["id"]==id);
  CHECK(c.m_current["payload"]==report["payload"]);CHECK(c.m_current["status"]=="closed");
  CHECK(c.m_current["photos"].toArray()[0].toObject()["path"].toString().isEmpty());
  Lem restored(root);restored.demo();restored.openReport("",id);
  CHECK(restored.m_current["id"]==id && restored.m_current["payload"]==report["payload"]);
  CHECK(restored.m_current["photos"].toArray()[0].toObject()["path"].toString().isEmpty());
  qInfo()<<"PASS: independent mandatory readings, NOK notes, NA reset, optional serial photo, closed/pending cleanup and restart";return true;
 }
};
int main(int argc,char**argv){QGuiApplication app(argc,argv);app.setApplicationVersion("0.1.57");QTemporaryDir dir;Lem c(dir.path());return LemReviewTest::run(c,dir.path())?0:1;}
