#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include "CanFrame.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

class CanSniffer;  // forward‑declaration

class LuaScriptEngine : public QObject {
    Q_OBJECT
public:
    explicit LuaScriptEngine(QObject *parent = nullptr);
    ~LuaScriptEngine() override;

    bool loadScript(const QString &fileName);
    void unloadScript();
    bool isLoaded() const;

    // Ustawienie wskaźnika do sniffera (do sendFrame)
    void setSniffer(CanSniffer *sniffer);

public slots:
    void onNewFrame(const CanFrame &frame);

signals:
    void logMessage(const QString &msg);
    void errorOccurred(const QString &err);

private:
    static int api_sendFrame(lua_State *L);
    static int api_log(lua_State *L);
    static int api_getTick(lua_State *L);

    lua_State *m_lua = nullptr;
    CanSniffer *m_sniffer = nullptr;
    uint64_t m_startTick = 0;
};
