#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include "CanFrame.h"
#include "CanAlertEngine.h"

#ifdef HAS_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

class CanSniffer;

class LuaScriptEngine : public QObject {
    Q_OBJECT
public:
    explicit LuaScriptEngine(QObject *parent = nullptr);
    ~LuaScriptEngine() override;

    bool loadScript(const QString &fileName);
    bool loadScriptFromString(const QString &code, const QString &sourceName = "<editor>");
    void unloadScript();
    bool isLoaded() const { return m_loaded; }

    void setSniffer(CanSniffer *sniffer) { m_sniffer = sniffer; }

public slots:
    void onNewFrame(const CanFrame &frame);
    void callOnAlert(const CanAlert &alert);

signals:
    void logMessage(const QString &msg);
    void errorOccurred(const QString &err);

private:
#ifdef HAS_LUA
    static int api_sendFrame(lua_State *L);
    static int api_log(lua_State *L);
    static int api_getTick(lua_State *L);
    lua_State *createState();
    lua_State *m_lua = nullptr;
#endif
    CanSniffer *m_sniffer = nullptr;
    uint64_t m_startTick = 0;
    bool m_loaded = false;
};
