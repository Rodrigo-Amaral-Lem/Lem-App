// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QByteArray>
#include <QString>

// One protected credential per application data profile. An unavailable or
// locked credential store leaves the app usable with a manual login.
namespace SessionStore {
QByteArray load(const QString &root);
bool save(const QString &root, const QByteArray &record);
bool remove(const QString &root);
}
