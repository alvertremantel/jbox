#include "windowcontroller.h"

#include <KWindowEffects>
#include <QGuiApplication>
#include <QWindow>

WindowController::WindowController(QObject *root, QObject *parent) : QObject(parent), m_win(root) {
    // Hide on startup; we only become visible when toggled.
    m_win->setProperty("visible", false);
    // We don't want the app to quit when the window is hidden — the
    // launcher stays running in the background until explicitly quit.
    qApp->setQuitOnLastWindowClosed(false);

    // Ask KWin to blur the desktop behind the panel, the same way it does
    // for KRunner and the Plasma panels. Without this, our translucent
    // background is a flat alpha blend straight onto the wallpaper, which
    // picks up whatever colors happen to be behind it instead of reading as
    // neutral Breeze Dark/Light. `create()` forces the native window handle
    // to exist so the KWin hint can be set even though we start hidden.
    if (auto *window = qobject_cast<QWindow *>(root)) {
        window->create();
        KWindowEffects::enableBlurBehind(window, true);
    }
}

void WindowController::toggle() {
    if (m_win->property("visible").toBool()) {
        m_win->setProperty("visible", false);
    } else {
        m_win->setProperty("visible", true);
    }
}

void WindowController::show() { m_win->setProperty("visible", true); }

void WindowController::hide() { m_win->setProperty("visible", false); }
