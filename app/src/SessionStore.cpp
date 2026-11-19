// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "SessionStore.h"

#if defined(Q_OS_WIN) || defined(Q_OS_ANDROID)
#include <QFile>
#include <QSaveFile>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#include <wincrypt.h>
#else
#include <QJniEnvironment>
#include <QJniObject>
#endif
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include <Security/Security.h>
#include <QCryptographicHash>
#include <QDir>
#elif defined(Q_OS_LINUX)
#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#endif

namespace {
constexpr qsizetype maxRecord = 16 * 1024;

#if defined(Q_OS_WIN) || defined(Q_OS_ANDROID)
QString recordPath(const QString &root) {
    return root + "/.session.dat";
}

QByteArray protect(const QByteArray &value, bool encrypt) {
    if (value.isEmpty() || value.size() > maxRecord)
        return {};
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(value.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(value.constData()))};
    DATA_BLOB output{};
    const auto flags = CRYPTPROTECT_UI_FORBIDDEN;
    const auto ok = encrypt
        ? CryptProtectData(&input, L"Lem session", nullptr, nullptr, nullptr, flags, &output)
        : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, flags, &output);
    if (!ok)
        return {};
    const QByteArray bytes(reinterpret_cast<const char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return bytes;
#else
    QJniEnvironment env;
    const auto input = env->NewByteArray(jsize(value.size()));
    if (!input) {
        env.checkAndClearExceptions();
        return {};
    }
    env->SetByteArrayRegion(input, 0, jsize(value.size()),
                            reinterpret_cast<const jbyte *>(value.constData()));
    const auto result = QJniObject::callStaticObjectMethod(
        "pt/lem/app/SessionCrypto", encrypt ? "seal" : "unseal", "([B)[B", input);
    env->DeleteLocalRef(input);
    if (!result.isValid())
        return {};
    const auto array = result.object<jbyteArray>();
    QByteArray bytes(env->GetArrayLength(array), Qt::Uninitialized);
    env->GetByteArrayRegion(array, 0, jsize(bytes.size()), reinterpret_cast<jbyte *>(bytes.data()));
    return bytes;
#endif
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
QByteArray profileKey(const QString &root) {
    return QCryptographicHash::hash(QDir(root).absolutePath().toUtf8(),
                                    QCryptographicHash::Sha256).toHex();
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
CFMutableDictionaryRef keychainQuery(const QString &root) {
    const auto profile = profileKey(root);
    const auto account = CFStringCreateWithCString(nullptr, profile.constData(), kCFStringEncodingASCII);
    const auto query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("pt.lem.app.session"));
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(account);
    return query;
}
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
// Pass secrets through pipes, including for store, so process arguments and
// diagnostics only contain the profile key. Never fall back to a plaintext file.
bool secretTool(const QString &root, QStringList args, const QByteArray &input,
                QByteArray *output = nullptr) {
    args << "application" << "pt.lem.app.session" << "profile" << QString::fromLatin1(profileKey(root));
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start("secret-tool", args);
    if (!process.waitForStarted(1000))
        return false;
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(2500)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
#endif
}

QByteArray SessionStore::load(const QString &root) {
#if defined(Q_OS_WIN) || defined(Q_OS_ANDROID)
    QFile file(recordPath(root));
    if (file.open(QIODevice::ReadOnly) && file.size() <= maxRecord)
        return protect(file.read(maxRecord + 1), false);
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    const auto query = keychainQuery(root);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status == errSecSuccess) {
        const auto data = static_cast<CFDataRef>(result);
        const auto size = CFDataGetLength(data);
        QByteArray bytes;
        if (size <= maxRecord)
            bytes = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), size);
        CFRelease(data);
        return bytes;
    }
#elif defined(Q_OS_LINUX)
    QByteArray output;
    if (secretTool(root, {"lookup"}, {}, &output) && output.size() <= maxRecord * 2)
        return QByteArray::fromBase64(output.trimmed(), QByteArray::AbortOnBase64DecodingErrors);
#endif
    return {};
}

bool SessionStore::save(const QString &root, const QByteArray &record) {
    if (record.isEmpty() || record.size() > maxRecord)
        return false;
#if defined(Q_OS_WIN) || defined(Q_OS_ANDROID)
    const auto bytes = protect(record, true);
    QSaveFile file(recordPath(root));
    return !bytes.isEmpty() && bytes.size() <= maxRecord && file.open(QIODevice::WriteOnly) &&
           file.write(bytes) == bytes.size() && file.commit();
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    const auto query = keychainQuery(root);
    const auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(record.constData()),
                                   record.size());
    CFDictionarySetValue(query, kSecValueData, data);
    auto status = SecItemAdd(query, nullptr);
    if (status == errSecDuplicateItem) {
        CFDictionaryRemoveValue(query, kSecValueData);
        const void *key = kSecValueData;
        const void *value = data;
        const auto attributes = CFDictionaryCreate(nullptr, &key, &value, 1,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
        status = SecItemUpdate(query, attributes);
        CFRelease(attributes);
    }
    CFRelease(data);
    CFRelease(query);
    return status == errSecSuccess;
#elif defined(Q_OS_LINUX)
    return secretTool(root, {"store", "--label=Lem session"}, record.toBase64());
#else
    return false;
#endif
}

bool SessionStore::remove(const QString &root) {
#if defined(Q_OS_WIN) || defined(Q_OS_ANDROID)
    const auto path = recordPath(root);
    return QFile::remove(path) || !QFile::exists(path);
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    const auto query = keychainQuery(root);
    const auto status = SecItemDelete(query);
    CFRelease(query);
    return status == errSecSuccess || status == errSecItemNotFound;
#elif defined(Q_OS_LINUX)
    return secretTool(root, {"clear"}, {});
#else
    return false;
#endif
}
