#include "LuaScriptEngine.h"
#include "CanSniffer.h"
#include <QFile>
#include <QDebug>
#include <QDateTime>

LuaScriptEngine::LuaScriptEngine(QObject *parent) : QObject(parent) {
    m_startTick = QDateTime::currentMSecsSinceEpoch();
}

LuaScriptEngine::~LuaScriptEngine() {
    unloadScript();
}

bool LuaScriptEngine::loadScript(const QString &fileName) {
    unloadScript();

    m_lua = luaL_newstate();
    if (!m_lua) {
        emit errorOccurred("Nie udało się utworzyć stanu Lua");
        return false;
    }

    luaL_openlibs(m_lua);

    // Rejestracja API jako domknięć z upvalue – wskaźnik do tej instancji
    lua_pushlightuserdata(m_lua, this);
    lua_pushcclosure(m_lua, api_sendFrame, 1);
    lua_setglobal(m_lua, "sendFrame");

    lua_pushlightuserdata(m_lua, this);
    lua_pushcclosure(m_lua, api_log, 1);
    lua_setglobal(m_lua, "log");

    lua_pushlightuserdata(m_lua, this);
    lua_pushcclosure(m_lua, api_getTick, 1);
    lua_setglobal(m_lua, "getTick");

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Nie można otworzyć pliku: " + fileName);
        unloadScript();
        return false;
    }

    QByteArray script = file.readAll();
    file.close();

    if (luaL_dostring(m_lua, script.constData()) != LUA_OK) {
        const char *err = lua_tostring(m_lua, -1);
        emit errorOccurred(QString::fromUtf8(err));
        unloadScript();
        return false;
    }

    lua_getglobal(m_lua, "onFrame");
    if (!lua_isfunction(m_lua, -1)) {
        lua_pop(m_lua, 1);
        emit errorOccurred("Skrypt nie zawiera funkcji 'onFrame(id, data, timestamp)'");
        unloadScript();
        return false;
    }
    lua_pop(m_lua, 1);

    emit logMessage("Skrypt Lua załadowany: " + fileName);
    return true;
}

void LuaScriptEngine::unloadScript() {
    if (m_lua) {
        lua_close(m_lua);
        m_lua = nullptr;
    }
}

bool LuaScriptEngine::isLoaded() const {
    return m_lua != nullptr;
}

void LuaScriptEngine::setSniffer(CanSniffer *sniffer) {
    m_sniffer = sniffer;
}

void LuaScriptEngine::onNewFrame(const CanFrame &frame) {
    if (!m_lua) return;

    lua_getglobal(m_lua, "onFrame");
    if (!lua_isfunction(m_lua, -1)) {
        lua_pop(m_lua, 1);
        return;
    }

    lua_pushinteger(m_lua, frame.id);
    lua_newtable(m_lua);
    for (int i = 0; i < frame.dlc; ++i) {
        lua_pushinteger(m_lua, frame.data[i]);
        lua_rawseti(m_lua, -2, i + 1);
    }
    lua_pushinteger(m_lua, frame.timestamp);

    if (lua_pcall(m_lua, 3, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(m_lua, -1);
        emit errorOccurred(QString("Błąd w onFrame: %1").arg(err));
        lua_pop(m_lua, 1);
    }
}

// --- API dla skryptów Lua (prawidłowe domknięcia) ---
int LuaScriptEngine::api_sendFrame(lua_State *L) {
    LuaScriptEngine *engine = (LuaScriptEngine*)lua_touserdata(L, lua_upvalueindex(1));
    if (!engine) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Brak kontekstu LuaScriptEngine");
        return 2;
    }
    if (!engine->m_sniffer) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Sniffer nie jest podłączony");
        return 2;
    }
    if (!engine->m_sniffer->isSocketValid()) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "Socket CAN nie jest otwarty");
        return 2;
    }

    uint32_t id = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    size_t len = lua_rawlen(L, 2);
    if (len > 64) len = 64;

    CanFrame frame;
    frame.id = id;
    frame.dlc = (uint8_t)len;
    for (size_t i = 0; i < len; ++i) {
        lua_rawgeti(L, 2, i + 1);
        frame.data[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    engine->m_sniffer->writeFrame(frame);
    lua_pushboolean(L, 1);
    return 1;
}

int LuaScriptEngine::api_log(lua_State *L) {
    LuaScriptEngine *engine = (LuaScriptEngine*)lua_touserdata(L, lua_upvalueindex(1));
    if (!engine) return 0;
    const char *msg = luaL_checkstring(L, 1);
    emit engine->logMessage(QString::fromUtf8(msg));
    return 0;
}

int LuaScriptEngine::api_getTick(lua_State *L) {
    LuaScriptEngine *engine = (LuaScriptEngine*)lua_touserdata(L, lua_upvalueindex(1));
    if (!engine) {
        lua_pushinteger(L, 0);
        return 1;
    }
    uint64_t now = QDateTime::currentMSecsSinceEpoch();
    lua_pushinteger(L, now - engine->m_startTick);
    return 1;
}
