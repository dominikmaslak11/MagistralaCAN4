#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <X11/Xlib.h>

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    bool registerShortcut(const QKeySequence &shortcut);
    void unregisterShortcut();

signals:
    void activated();

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    unsigned int m_nativeModifiers = 0;
    KeyCode m_nativeKeyCode = 0;
    bool m_registered = false;
    Display *m_display = nullptr;
};
