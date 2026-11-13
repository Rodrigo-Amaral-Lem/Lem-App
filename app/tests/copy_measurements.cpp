#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QElapsedTimer>
#include <QDebug>
#define CHECK(x) do {if(!(x)){qCritical()<<"FAIL"<<__LINE__<<#x;return false;}} while(false)
struct LemReviewTest {
 static bool wait(Lem &c){QElapsedTimer t;t.start();while((!c.m_copyScope.isEmpty()||!c.m_pendingCopySource.isEmpty()||c.attachingFiles())&&t.elapsed()<3000)QCoreApplication::processEvents();return c.m_copyScope.isEmpty()&&!c.attachingFiles();}
 static bool run(Lem &c,const QString &root,const QString &clip){
  c.demo();c.m_server="https://unused.invalid";c.m_token="do-not-request-network";
  const auto sourceId=c.m_current["id"].toString();
  c.answer("QVE.01","OK","Local cached QVE");
  for(int i=4;i<=10;++i)c.answer(QString("MAN.%1").arg(i,2,10,QChar('0')),i<7?"400 V":"230 V","cached reading");
  QImage image(50,40,QImage::Format_RGB32);image.fill(Qt::cyan);const auto path=root+"/image.png";CHECK(image.save(path));
  c.attach("QVE.01",QUrl::fromLocalFile(path));c.attach("QVE.04",QUrl::fromLocalFile(clip));c.attach("MAN.04",QUrl::fromLocalFile(path));
  CHECK(c.flushEdits());auto source=c.m_current;source["dirty"]=false;
  c.m_reports[sourceId]=source;
  auto second=c.m_chargers[0].toObject();second["id"]="second";second["external_id"]="ID-SECOND";c.m_chargers.append(second);
  auto third=second;third["id"]="third";third["external_id"]="ID-THIRD";c.m_chargers.append(third);
  auto measurementSource=source;measurementSource["id"]="measurement-source";measurementSource["charger_id"]="third";
  c.m_reports["measurement-source"]=measurementSource;
  c.openReport("second");const auto targetId=c.m_current["id"].toString();
  c.answer("MAN.01","Software stays","");c.answer("MAN.18","120 Ω","");
  c.copyQveFrom(sourceId);CHECK(!c.busy()&&c.copyingGroup()=="QVE");
  c.copyMeasurementsFrom("measurement-source");CHECK(c.m_pendingCopyScope=="MAN");
  c.answer("EST.06","OK","Edited while copying QVE");
  c.answer("QVE.02","NOK","Must be ignored while target group is locked");
  CHECK(!c.m_current["payload"].toObject()["answers"].toObject().contains("QVE.02"));
  c.openReport("demo-charger",sourceId);CHECK(c.m_current["id"].toString()==targetId);
  c.finish();CHECK(c.m_current["status"]=="draft");
  CHECK(wait(c));
  CHECK(c.m_current["payload"].toObject()["answers"].toObject()["EST.06"].toObject()["notes"]=="Edited while copying QVE");
  CHECK(c.m_current["payload"].toObject()["answers"].toObject()["QVE.01"].toObject()["notes"]=="Local cached QVE");
  CHECK(c.m_current["payload"].toObject()["measurement_copy"].toObject()["source_report_id"]=="measurement-source");
  CHECK(c.m_transfers.isEmpty()); // clean cached source must never be fetched over HTTP
  c.copyMeasurementsFrom(sourceId);CHECK(!c.busy()&&c.copyingGroup()=="MAN");
  c.answer("MAN.18","NOK","Earth test failed");c.answer("MAN.04","999 V","Must be ignored");c.answer("QVE.02","OK","Edited while copying measurements");
  CHECK(wait(c));
  const auto a=c.m_current["payload"].toObject()["answers"].toObject();
  CHECK(a["MAN.04"].toObject()["value"]=="400 V");CHECK(a["MAN.10"].toObject()["notes"]=="cached reading");
  CHECK(a["MAN.01"].toObject()["value"]=="Software stays"&&a["MAN.18"].toObject()["value"]=="NOK");
  CHECK(a["QVE.02"].toObject()["notes"]=="Edited while copying measurements");
  CHECK(c.m_current["payload"].toObject()["measurement_copy"].toObject()["source_report_id"]==sourceId);
  CHECK(c.m_current["photos"].toArray().size()==3);
  for(auto target:c.m_current["photos"].toArray())for(auto original:source["photos"].toArray())CHECK(target.toObject()["id"]!=original.toObject()["id"]);
  CHECK(c.m_reports[sourceId].toObject()["payload"]==source["payload"]);
  QFile bad(root+"/bad.png");CHECK(bad.open(QIODevice::WriteOnly));bad.write("invalid");bad.close();
  c.attachMany("EST.01",{QUrl::fromLocalFile(path),QUrl::fromLocalFile(bad.fileName()),QUrl::fromLocalFile(path),QUrl::fromLocalFile(clip)});
  CHECK(c.attachingFiles()&&!c.busy());c.answer("EST.03","Still editable during batch","");
  CHECK(wait(c));CHECK(c.m_attachDone==3&&c.m_attachFailed==1);CHECK(c.m_current["photos"].toArray().size()==6);
  CHECK(c.m_current["payload"].toObject()["answers"].toObject()["EST.03"].toObject()["value"]=="Still editable during batch");
  c.m_token.clear();c.m_autoSync.stop();
  qInfo()<<"PASS: clean local QVE preferred, independent editing, measurement subset/evidence copy, source isolation, multi-file batch with invalid file";
  return true;
 }
};
int main(int argc,char **argv){QGuiApplication app(argc,argv);app.setApplicationVersion("0.1.57");QTemporaryDir root;Lem c(root.path());return LemReviewTest::run(c,root.path(),argc>1?QString::fromLocal8Bit(argv[1]):QString())?0:1;}
