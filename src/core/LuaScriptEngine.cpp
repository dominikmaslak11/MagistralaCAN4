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

// ── Wspólne jądro: inicjuje stan Lua i rejestruje API ────────────────────────
#ifdef HAS_LUA
lua_State *LuaScriptEngine::createState() {
    lua_State *L = luaL_newstate();
    if (!L) return nullptr;
    luaL_openlibs(L);

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, api_sendFrame, 1);
    lua_setglobal(L, "sendFrame");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, api_log, 1);
    lua_setglobal(L, "log");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, api_getTick, 1);
    lua_setglobal(L, "getTick");

    return L;
}
#endif

bool LuaScriptEngine::loadScriptFromString(const QString &code, const QString &sourceName) {
#ifdef HAS_LUA
    unloadScript();

    m_lua = createState();
    if (!m_lua) {
        emit errorOccurred("Nie udało się utworzyć stanu Lua");
        return false;
    }

    QByteArray src  = code.toUtf8();
    QByteArray name = ("@" + sourceName).toUtf8();
    if (luaL_loadbuffer(m_lua, src.constData(), src.size(), name.constData()) != LUA_OK
     || lua_pcall(m_lua, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char *err = lua_tostring(m_lua, -1);
        emit errorOccurred(QString::fromUtf8(err ? err : "Nieznany błąd Lua"));
        unloadScript();
        return false;
    }

    lua_getglobal(m_lua, "onFrame");
    bool hasOnFrame = lua_isfunction(m_lua, -1);
    lua_pop(m_lua, 1);
    if (!hasOnFrame) {
        emit errorOccurred("Skrypt nie zawiera funkcji 'onFrame(id, data, timestamp)'");
        unloadScript();
        return false;
    }

    m_loaded = true;
    emit logMessage(QString("Skrypt Lua załadowany z: %1").arg(sourceName));
    return true;
#else
    Q_UNUSED(code); Q_UNUSED(sourceName);
    emit errorOccurred("Lua nie jest dostępne na tej platformie");
    return false;
#endif
}

bool LuaScriptEngine::loadScript(const QString &fileName) {
#ifdef HAS_LUA
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Nie można otworzyć pliku: " + fileName);
        return false;
    }
    QString code = QString::fromUtf8(file.readAll());
    file.close();
    return loadScriptFromString(code, fileName);
#else
    Q_UNUSED(fileName);
    emit errorOccurred("Lua nie jest dostępne na tej platformie");
    return false;
#endif
}

void LuaScriptEngine::unloadScript() {
#ifdef HAS_LUA
    if (m_lua) {
        lua_close(m_lua);
        m_lua = nullptr;
    }
#endif
    m_loaded = false;
}

void LuaScriptEngine::onNewFrame(const CanFrame &frame) {
#ifdef HAS_LUA
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
    lua_pushinteger(m_lua, static_cast<lua_Integer>(frame.timestamp));

    if (lua_pcall(m_lua, 3, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(m_lua, -1);
        emit errorOccurred(QString("Błąd w onFrame: %1").arg(err));
        lua_pop(m_lua, 1);
    }
#else
    Q_UNUSED(frame);
#endif
}

void LuaScriptEngine::callOnAlert(const CanAlert &alert) {
#ifdef HAS_LUA
    if (!m_lua) return;

    lua_getglobal(m_lua, "onAlert");
    if (!lua_isfunction(m_lua, -1)) {
        lua_pop(m_lua, 1);
        return;  // onAlert is optional
    }

    lua_pushstring(m_lua, alert.ruleName.toUtf8().constData());
    lua_pushinteger(m_lua, alert.frame.id);
    lua_pushstring(m_lua, alert.description.toUtf8().constData());

    // data table
    lua_newtable(m_lua);
    for (int i = 0; i < alert.frame.dlc && i < 64; ++i) {
        lua_pushinteger(m_lua, alert.frame.data[i]);
        lua_rawseti(m_lua, -2, i + 1);
    }

    lua_pushinteger(m_lua, static_cast<lua_Integer>(alert.timestampUs));

    if (lua_pcall(m_lua, 5, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(m_lua, -1);
        emit errorOccurred(QString("Błąd w onAlert: %1").arg(err));
        lua_pop(m_lua, 1);
    }
#else
    Q_UNUSED(alert);
#endif
}

#ifdef HAS_LUA
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
    frame.dlc = static_cast<uint8_t>(len);
    for (size_t i = 0; i < len; ++i) {
        lua_rawgeti(L, 2, static_cast<lua_Integer>(i + 1));
        frame.data[i] = static_cast<uint8_t>(lua_tointeger(L, -1));
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
    lua_pushinteger(L, static_cast<lua_Integer>(now - engine->m_startTick));
    return 1;
}
#endif
