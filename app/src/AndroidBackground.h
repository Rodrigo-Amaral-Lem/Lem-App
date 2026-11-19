// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Lem.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

// Qt and Android share one process but have different main threads. Job callbacks
// only queue work on the Qt thread; the existing Lem remains the sole cache writer.
class AndroidBackground : public QObject {
public:
    explicit AndroidBackground(Lem &lem) : m_lem(lem) {
        owner = this;
        QJniEnvironment env;
        const JNINativeMethod method{"jobChanged", "(IZ)V", reinterpret_cast<void *>(jobChanged)};
        m_registered = env.registerNativeMethods(jobClass, &method, 1);
        if (!m_registered) qWarning() << "Lem: registo do agendador Android indisponível";
        connect(&lem, &Lem::changed, this, [this] { update(); });
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](auto state) {
            update();
            if (state == Qt::ApplicationActive && m_transfer)
                QJniObject::callStaticMethod<void>(transferClass, "permissionChanged", "()V");
        });
        m_check.setSingleShot(true);
        connect(&m_check, &QTimer::timeout, this, [this] { checkJob(); });
        update();
    }
    ~AndroidBackground() override {
        owner = nullptr;
        if (m_transfer) setFlag(transferClass, "setNeeded", false);
        if (m_scheduled) setFlag(jobClass, "setEnabled", false);
    }
private:
    static constexpr auto jobClass = "pt/lem/app/CatalogueJob";
    static constexpr auto transferClass = "pt/lem/app/TransferService";
    inline static AndroidBackground *owner = nullptr; // Read only on the Qt thread.
    static void JNICALL jobChanged(JNIEnv *, jclass, jint ticket, jboolean started) {
        QMetaObject::invokeMethod(qApp, [ticket, started] {
            if (!owner) return;
            if (started) {
                owner->m_ticket = ticket;
                owner->m_started = false;
                owner->m_check.start(0);
            } else if (owner->m_ticket == ticket) {
                owner->m_ticket = 0;
                owner->m_check.stop();
            }
        }, Qt::QueuedConnection);
    }
    static void setFlag(const char *type, const char *method, bool value) {
        auto context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(type, method, "(Landroid/content/Context;Z)V",
                                            context.object(), jboolean(value));
    }
    void update() {
        // Register while the session is active. Waiting for onStop to register
        // loses the job if Android freezes the activity during that transition.
        const bool scheduled = m_registered && m_lem.authenticated();
        if (scheduled != m_scheduled) {
            m_scheduled = scheduled;
            setFlag(jobClass, "setEnabled", scheduled);
        }
        const bool transfer = m_lem.authenticated() && m_lem.backgroundTransferActive() &&
                              (m_transfer || qGuiApp->applicationState() == Qt::ApplicationActive);
        if (transfer != m_transfer) {
            m_transfer = transfer;
            setFlag(transferClass, "setNeeded", transfer);
        }
        if (!m_offered && m_lem.evidenceDownloadActive() &&
            qGuiApp->applicationState() == Qt::ApplicationActive) {
            m_offered = true;
            auto context = QNativeInterface::QAndroidApplication::context();
            QJniObject::callStaticMethod<void>(transferClass, "offerNotification",
                "(Landroid/content/Context;)V", context.object());
        }
        if (m_ticket) m_check.start(0);
    }
    void checkJob() {
        if (!m_ticket) return;
        if (m_scheduled && qGuiApp->applicationState() != Qt::ApplicationActive) {
            // Coalesce scheduler deliveries around visibility transitions.
            if (!m_started && (!m_last.isValid() || m_last.elapsed() >= 15 * 60 * 1000)) {
                if (!m_lem.refreshBackgroundCatalogues()) {
                    m_check.start(5000);
                    return;
                }
                m_started = true;
                m_last.start();
            }
            if (m_started && (m_lem.backgroundTransferActive() || m_lem.busy())) {
                m_check.start(5000);
                return;
            }
        }
        QJniObject::callStaticMethod<void>(jobClass, "finish", "(I)V", jint(m_ticket));
        m_ticket = 0;
    }
    Lem &m_lem;
    QTimer m_check;
    QElapsedTimer m_last;
    int m_ticket = 0;
    bool m_registered = false, m_scheduled = false, m_started = false, m_transfer = false;
    bool m_offered = false;
};
