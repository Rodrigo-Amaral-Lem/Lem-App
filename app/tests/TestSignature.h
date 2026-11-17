// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.
#pragma once
#include "Lem.h"
#include <QBuffer>
#include <QImage>

// Closure fixtures need a valid signature even when testing an unrelated rule.
inline bool addTestSignature(Lem &client) {
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(Qt::black);
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return false;
    client.answer("FIN.02", "Técnico de teste", "");
    return client.saveSignature("FIN.02", "data:image/png;base64," + QString::fromLatin1(png.toBase64()));
}
