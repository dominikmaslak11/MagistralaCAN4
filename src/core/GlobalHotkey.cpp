#include "GlobalHotkey.h"
#include <QGuiApplication>
#include <QDebug>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent) {
    qApp->installNativeEventFilter(this);
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        qWarning("GlobalHotkey: nie można otworzyć Display.");
    }
}

GlobalHotkey::~GlobalHotkey() {
    unregisterShortcut();
    qApp->removeNativeEventFilter(this);
    if (m_display) XCloseDisplay(m_display);
}

bool GlobalHotkey::registerShortcut(const QKeySequence &shortcut) {
    if (!m_display) return false;
    if (m_registered) unregisterShortcut();
    if (shortcut.isEmpty()) return false;

    QKeyCombination combo = shortcut[0];
    Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    Qt::Key key = combo.key();

    KeySym keysym = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        keysym = XK_a + (key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        keysym = XK_0 + (key - Qt::Key_0);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        keysym = XK_F1 + (key - Qt::Key_F1);
    else {
        qWarning() << "GlobalHotkey: nieobsługiwany klawisz" << key;
        return false;
    }

    KeyCode keycode = XKeysymToKeycode(m_display, keysym);
    if (keycode == 0) {
        qWarning("GlobalHotkey: klawisz nie ma keycode.");
        return false;
    }

    unsigned int nativeMods = 0;
    if (mods & Qt::ControlModifier) nativeMods |= ControlMask;
    if (mods & Qt::ShiftModifier)   nativeMods |= ShiftMask;
    if (mods & Qt::AltModifier)     nativeMods |= Mod1Mask;
    if (mods & Qt::MetaModifier)    nativeMods |= Mod4Mask;

    Window root = DefaultRootWindow(m_display);
    XGrabKey(m_display, keycode, nativeMods, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(m_display, keycode, nativeMods | LockMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(m_display, keycode, nativeMods | Mod2Mask, root, True, GrabModeAsync, GrabModeAsync);
    XSync(m_display, False);

    m_nativeModifiers = nativeMods;
    m_nativeKeyCode = keycode;
    m_registered = true;
    return true;
}

void GlobalHotkey::unregisterShortcut() {
    if (!m_display || !m_registered) return;
    Window root = DefaultRootWindow(m_display);
    XUngrabKey(m_display, m_nativeKeyCode, m_nativeModifiers, root);
    XUngrabKey(m_display, m_nativeKeyCode, m_nativeModifiers | LockMask, root);
    XUngrabKey(m_display, m_nativeKeyCode, m_nativeModifiers | Mod2Mask, root);
    XSync(m_display, False);
    m_registered = false;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType);
    if (!m_registered || !m_display) return false;

    xcb_generic_event_t *ev = static_cast<xcb_generic_event_t*>(message);
    if (ev && (ev->response_type & ~0x80) == XCB_KEY_PRESS) {
        xcb_key_press_event_t *kp = reinterpret_cast<xcb_key_press_event_t*>(ev);
        unsigned int state = kp->state & (ControlMask | ShiftMask | Mod1Mask | Mod4Mask);
        if (kp->detail == m_nativeKeyCode && state == m_nativeModifiers) {
            emit activated();
            *result = 1;
            return true;
        }
    }
    return false;
}
