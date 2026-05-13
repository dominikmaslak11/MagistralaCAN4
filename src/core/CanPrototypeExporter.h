#pragma once
#include "DbcParser.h"
#include <QString>
#include <QVector>
#include <cstdint>

/// Metadata for one CAN message to be exported as prototype code.
struct FrameExportInfo {
    uint32_t         id           = 0;
    bool             extended     = false;
    int              dlc          = 8;
    QVector<uint8_t> sampleData;        // last-seen payload (size == dlc)
    QString          name;              // from DBC; empty uses "0x%1"
    double           periodMs     = 0.0;
    double           jitterMs     = 0.0;
    QVector<int>     counterBytes;      // byte positions (0-based) with rolling +1 counter
    QVector<DbcSignal> sigList;          // DBC signal definitions; may be empty
};

/// Generates Python (python-can), Lua, or Arduino C++ prototype code
/// from CAN frame analysis results.
class CanPrototypeExporter {
public:
    enum class Format { Python, Lua, Arduino };

    struct Config {
        Format  format       = Format::Python;
        QString busType      = "socketcan";  // python-can: socketcan/pcan/usb2can/kvaser/slcan
        QString channel      = "can0";       // e.g. can0, PCAN_USBBUS1, /dev/ttyUSB0
        int     bitrate      = 500000;
        bool    withReceiver = false;
    };

    static QString generate(const QVector<FrameExportInfo> &frames, const Config &cfg);

private:
    static QString generatePython (const QVector<FrameExportInfo> &frames, const Config &cfg);
    static QString generateLua    (const QVector<FrameExportInfo> &frames, const Config &cfg);
    static QString generateArduino(const QVector<FrameExportInfo> &frames, const Config &cfg);

    // helpers
    static QString pythonClassName(const FrameExportInfo &f);
    static QString luaVarName     (const FrameExportInfo &f);
    static QString cppVarName     (const FrameExportInfo &f);
    static QString sanitize       (const QString &s);        // make valid identifier
    static QString hexId          (uint32_t id);             // "0x1A0"
    static QString sampleHex      (const FrameExportInfo &f);// "0x3C 0x05 ..."
    static QString sigComment     (const DbcSignal &sig, int indent);
    static QString pythonEncodeSignal(const DbcSignal &sig, int indent);
    static QString arduinoEncodeSignal(const DbcSignal &sig);
};
