// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Preference regression, independent of registry/INI backend used by each platform.
#include "ServerSettings.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#define CHECK(x) do { if(!(x)){qCritical()<<"FAILED"<<#x<<__LINE__;return 1;} } while(false)
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir dir;
    QSettings settings(dir.path()+"/settings.ini",QSettings::IniFormat);
    CHECK(ServerSettings::load(settings)=="https://lem.rodrod.pt");
    settings.clear();settings.setValue("server","https://previous.rodrod.pt/");
    CHECK(ServerSettings::load(settings)==ServerSettings::defaultUrl());
    CHECK(settings.value("server").toString()==ServerSettings::defaultUrl());
    CHECK(settings.value("serverSelectionSchema").toInt()==1);
    settings.clear();settings.setValue("server","https://PREVIOUS.RODROD.PT:443/");
    CHECK(ServerSettings::load(settings)==ServerSettings::defaultUrl());
    for(const auto &url:QStringList{"https://custom.example.org","https://previous.rodrod.pt:8443","https://previous.rodrod.pt/api","http://127.0.0.1:18765"}) {
        settings.clear();settings.setValue("server",url);
        CHECK(ServerSettings::load(settings)==url);
    }
    settings.clear();ServerSettings::save(settings,"https://explicit.rodrod.pt");
    CHECK(ServerSettings::load(settings)=="https://explicit.rodrod.pt");
    settings.clear();settings.setValue("server","");
    CHECK(ServerSettings::load(settings)==ServerSettings::defaultUrl());
    qInfo()<<"PASS: default, managed prototype alias migration, custom server preservation, explicit choices and blank setting";
}
