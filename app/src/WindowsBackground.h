// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Lem.h"
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWindow>
#include <QWinEventNotifier>
#include <qt_windows.h>

// A named auto-reset event makes launching the shortcut restore the resident
// window. Acquire it before opening the cache, even during simultaneous starts.
class WindowsInstance {
public:
    explicit WindowsInstance(const wchar_t *name = L"Local\\pt.lem.app.open-window") {
        event = CreateEventW(nullptr, FALSE, FALSE, name);
        existing = event && GetLastError() == ERROR_ALREADY_EXISTS;
        if (existing) SetEvent(event);
    }
    ~WindowsInstance() { if (event) CloseHandle(event); }
    Q_DISABLE_COPY(WindowsInstance)
    HANDLE event;
    bool existing;
};

class WindowsBackground : public QObject {
public:
    WindowsBackground(QWindow *window, Lem &lem, HANDLE activation)
        : m_window(window), m_activation(activation), m_tray(qApp->windowIcon()) {
        connect(&m_activation, &QWinEventNotifier::activated, this, [this] { restore(); });
        m_menu.addAction(tr("Abrir Lem"), this, [this] { restore(); });
        m_menu.addAction(tr("Sair"), qApp, &QApplication::quit);
        m_tray.setContextMenu(&m_menu);
        m_tray.setToolTip(tr("Lem"));
        connect(&m_tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                restore();
        });
        m_tray.show();
        window->installEventFilter(this);
        m_refresh.setSingleShot(true);
        connect(window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
            if (visibility == QWindow::Hidden || visibility == QWindow::Minimized) {
                m_refresh.start(m_lastRefresh.isValid()
                    ? qMax<qint64>(0, refreshInterval - m_lastRefresh.elapsed()) : 0);
            } else {
                m_refresh.stop();
            }
        });
        connect(&m_refresh, &QTimer::timeout, this, [this, &lem] {
            if (!lem.refreshBackgroundCatalogues()) {
                m_refresh.start(15000);
                return;
            }
            m_lastRefresh.start();
            m_refresh.start(refreshInterval);
        });
    }
private:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == m_window && event->type() == QEvent::Close &&
            QSystemTrayIcon::isSystemTrayAvailable() && !qApp->isSavingSession()) {
            static_cast<QCloseEvent *>(event)->ignore();
            m_window->hide();
            return true;
        }
        return false;
    }
    void restore() {
        m_refresh.stop();
        m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
    }
    static constexpr int refreshInterval = 15 * 60 * 1000;
    QWindow *m_window;
    QWinEventNotifier m_activation;
    QMenu m_menu;
    QSystemTrayIcon m_tray;
    QTimer m_refresh;
    QElapsedTimer m_lastRefresh;
};
