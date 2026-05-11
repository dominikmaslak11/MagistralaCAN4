#include "PcanDriver.h"
#include <QDebug>
#include <QLibrary>
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// Implementacja Windows (PIMPL – ukryta za wskaźnikiem)
// ═══════════════════════════════════════════════════════════════
#ifdef Q_OS_WIN
#include <windows.h>

#define PCAN_CHANNEL_USBBUS1     0x51
#define PCAN_CHANNEL_USBBUS2     0x52
#define PCAN_CHANNEL_USBBUS3     0x53
#define PCAN_CHANNEL_USBBUS4     0x54
#define PCAN_CHANNEL_USBBUS5     0x55
#define PCAN_CHANNEL_USBBUS6     0x56
#define PCAN_CHANNEL_USBBUS7     0x57
#define PCAN_CHANNEL_USBBUS8     0x58
#define PCAN_BAUD_1M             0x0014
#define PCAN_BAUD_800K           0x0016
#define PCAN_BAUD_500K           0x001C
#define PCAN_BAUD_250K           0x011C
#define PCAN_BAUD_125K           0x031C
#define PCAN_BAUD_100K           0x432F
#define PCAN_BAUD_50K            0x472F
#define PCAN_BAUD_20K            0x532F
#define PCAN_BAUD_10K            0x672F
#define PCAN_MESSAGE_STANDARD    0x00
#define PCAN_MESSAGE_EXTENDED    0x02
#define PCAN_MESSAGE_FD          0x10
#define PCAN_MESSAGE_RTR         0x01
#define PCAN_CHANNEL_CONDITION   0x11
#define PCAN_CHANNEL_AVAILABLE   0x01
#define PCAN_CHANNEL_OCCUPIED    0x02
#define PCAN_CHANNEL_INVALID     0x04

typedef struct { DWORD ID; BYTE MSGTYPE; BYTE LEN; BYTE DATA[8]; } TPCANMsg;
typedef struct { DWORD ID; DWORD MSGTYPE; DWORD DLC; BYTE DATA[64]; } TPCANMsgFD;
typedef ULONGLONG TPCANTimestamp;
typedef BYTE TPCANHandle;

struct PcanDriver::Impl {
    QLibrary lib;
    TPCANHandle channel = 0;
    WORD     baudCode = PCAN_BAUD_500K;
    bool initialized = false;

    typedef int (__stdcall *CAN_Init_t)(TPCANHandle, WORD, DWORD, DWORD);
    typedef int (__stdcall *CAN_Uninit_t)(TPCANHandle);
    typedef int (__stdcall *CAN_Read_t)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
    typedef int (__stdcall *CAN_ReadFD_t)(TPCANHandle, TPCANMsgFD*, TPCANTimestamp*);
    typedef int (__stdcall *CAN_Write_t)(TPCANHandle, TPCANMsg*);
    typedef int (__stdcall *CAN_WriteFD_t)(TPCANHandle, TPCANMsgFD*);
    typedef int (__stdcall *CAN_GetStatus_t)(TPCANHandle);
    typedef int (__stdcall *CAN_GetValue_t)(TPCANHandle, DWORD, void*, DWORD*);
    typedef int (__stdcall *CAN_GetErr_t)(int, WORD, char*);

    CAN_Init_t      fnInit   = nullptr;
    CAN_Uninit_t    fnUninit = nullptr;
    CAN_Read_t      fnRead   = nullptr;
    CAN_ReadFD_t    fnReadFD = nullptr;
    CAN_Write_t     fnWrite  = nullptr;
    CAN_WriteFD_t   fnWriteFD= nullptr;
    CAN_GetStatus_t fnStatus = nullptr;
    CAN_GetValue_t  fnGetValue = nullptr;
    CAN_GetErr_t    fnErrTxt = nullptr;
};

PcanDriver::PcanDriver() { d = new Impl; }
PcanDriver::~PcanDriver() { close(); delete d; d = nullptr; }

bool PcanDriver::open(const QString &device) {
    if (!d) return false;

    //  Załaduj DLL
    d->lib.setFileName("PCANBasic");
    if (!d->lib.load()) {
        qWarning() << "PcanDriver:" << d->lib.errorString();
        return false;
    }
    d->fnInit   = (Impl::CAN_Init_t)  d->lib.resolve("CAN_Initialize");
    d->fnUninit = (Impl::CAN_Uninit_t)d->lib.resolve("CAN_Uninitialize");
    d->fnRead   = (Impl::CAN_Read_t)  d->lib.resolve("CAN_Read");
    d->fnReadFD = (Impl::CAN_ReadFD_t)d->lib.resolve("CAN_ReadFD");
    d->fnWrite  = (Impl::CAN_Write_t) d->lib.resolve("CAN_Write");
    d->fnWriteFD= (Impl::CAN_WriteFD_t)d->lib.resolve("CAN_WriteFD");
    d->fnStatus   = (Impl::CAN_GetStatus_t)d->lib.resolve("CAN_GetStatus");
    d->fnGetValue = (Impl::CAN_GetValue_t)d->lib.resolve("CAN_GetValue");
    d->fnErrTxt   = (Impl::CAN_GetErr_t)  d->lib.resolve("CAN_GetErrorText");
    if (!d->fnInit || !d->fnRead || !d->fnWrite) {
        qWarning() << "PcanDriver: brak wymaganych funkcji w PCANBasic.dll";
        d->lib.unload();
        return false;
    }

    //  Wybierz kanał
    if (device == "PCAN_USBBUS2") d->channel = PCAN_CHANNEL_USBBUS2;
    else if (device == "PCAN_USBBUS3") d->channel = PCAN_CHANNEL_USBBUS3;
    else if (device == "PCAN_USBBUS4") d->channel = PCAN_CHANNEL_USBBUS4;
    else if (device == "PCAN_USBBUS5") d->channel = PCAN_CHANNEL_USBBUS5;
    else if (device == "PCAN_USBBUS6") d->channel = PCAN_CHANNEL_USBBUS6;
    else if (device == "PCAN_USBBUS7") d->channel = PCAN_CHANNEL_USBBUS7;
    else if (device == "PCAN_USBBUS8") d->channel = PCAN_CHANNEL_USBBUS8;
    else d->channel = PCAN_CHANNEL_USBBUS1;

    int res = d->fnInit(d->channel, d->baudCode, 0, 0);
    if (res != 0) {
        char buf[256] = {};
        if (d->fnErrTxt) d->fnErrTxt(res, 0x0409, buf);
        qWarning() << "PcanDriver: CAN_Initialize error:" << buf;
        d->lib.unload();
        return false;
    }
    d->initialized = true;
    qDebug() << "PcanDriver: otwarto" << device;
    return true;
}

void PcanDriver::close() {
    if (!d) return;
    if (d->initialized && d->fnUninit) d->fnUninit(d->channel);
    d->initialized = false;
    if (d->lib.isLoaded()) d->lib.unload();
}

bool PcanDriver::isValid() const { return d && d->initialized; }

CanFrame PcanDriver::readFrame() {
    if (!d || !d->initialized) return {};

    // Try classic CAN read first
    if (d->fnRead) {
        TPCANMsg msg; memset(&msg, 0, sizeof(msg));
        TPCANTimestamp ts = 0;
        int res = d->fnRead(d->channel, &msg, &ts);
        if (res == 0) {
            CanFrame f;
            f.id = msg.ID & 0x1FFFFFFF;
            f.extended = msg.MSGTYPE & PCAN_MESSAGE_EXTENDED;
            f.rtr      = msg.MSGTYPE & PCAN_MESSAGE_RTR;
            f.dlc = msg.LEN > 8 ? 8 : msg.LEN;
            memcpy(f.data.data(), msg.DATA, f.dlc);
            f.timestamp = ts;
            return f;
        }
    }

    // Try FD read if classic returned no data
    if (d->fnReadFD) {
        TPCANMsgFD msg; memset(&msg, 0, sizeof(msg));
        TPCANTimestamp ts = 0;
        int res = d->fnReadFD(d->channel, &msg, &ts);
        if (res == 0) {
            CanFrame f;
            f.id = msg.ID & 0x1FFFFFFF;
            f.extended = msg.MSGTYPE & PCAN_MESSAGE_EXTENDED;
            f.rtr      = msg.MSGTYPE & PCAN_MESSAGE_RTR;
            f.fd       = msg.MSGTYPE & PCAN_MESSAGE_FD;
            f.dlc = msg.DLC > 64 ? 64 : msg.DLC;
            memcpy(f.data.data(), msg.DATA, f.dlc);
            f.timestamp = ts;
            return f;
        }
    }
    return {};
}

void PcanDriver::writeFrame(const CanFrame &frame) {
    if (!d || !d->initialized) return;

    if (frame.fd && d->fnWriteFD) {
        TPCANMsgFD msg; memset(&msg, 0, sizeof(msg));
        msg.ID = frame.id;
        msg.MSGTYPE = frame.extended ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD;
        msg.MSGTYPE |= PCAN_MESSAGE_FD;
        if (frame.rtr) msg.MSGTYPE |= PCAN_MESSAGE_RTR;
        msg.DLC = frame.dlc > 64 ? 64 : frame.dlc;
        memcpy(msg.DATA, frame.data.data(), msg.DLC);
        d->fnWriteFD(d->channel, &msg);
    } else if (d->fnWrite) {
        TPCANMsg msg; memset(&msg, 0, sizeof(msg));
        msg.ID = frame.id;
        msg.MSGTYPE = frame.extended ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD;
        if (frame.rtr) msg.MSGTYPE |= PCAN_MESSAGE_RTR;
        msg.LEN = frame.dlc > 8 ? 8 : frame.dlc;
        memcpy(msg.DATA, frame.data.data(), msg.LEN);
        d->fnWrite(d->channel, &msg);
    }
}

void PcanDriver::setBaudRate(const QString &baudStr) {
    if (!d) return;
    QString b = baudStr.toUpper().trimmed();
    if (b == "1M" || b == "1000K")      d->baudCode = PCAN_BAUD_1M;
    else if (b == "800K")               d->baudCode = PCAN_BAUD_800K;
    else if (b == "500K")               d->baudCode = PCAN_BAUD_500K;
    else if (b == "250K")               d->baudCode = PCAN_BAUD_250K;
    else if (b == "125K")               d->baudCode = PCAN_BAUD_125K;
    else if (b == "100K")               d->baudCode = PCAN_BAUD_100K;
    else if (b == "50K")                d->baudCode = PCAN_BAUD_50K;
    else if (b == "20K")                d->baudCode = PCAN_BAUD_20K;
    else if (b == "10K")                d->baudCode = PCAN_BAUD_10K;
    qDebug() << "PcanDriver: baud rate set to" << b;
}

QStringList PcanDriver::availableDevices() const {
    QStringList devices;

    // Wczytaj PCANBasic.dll tymczasowo do detekcji sprzętu
    QLibrary lib("PCANBasic");
    if (!lib.load()) {
        qDebug() << "PcanDriver::availableDevices: nie można załadować PCANBasic.dll —"
                 << lib.errorString();
        return devices;
    }

    auto fnInit   = (Impl::CAN_Init_t)  lib.resolve("CAN_Initialize");
    auto fnUninit = (Impl::CAN_Uninit_t)lib.resolve("CAN_Uninitialize");
    if (!fnInit || !fnUninit) {
        qDebug() << "PcanDriver::availableDevices: brak CAN_Initialize/Uninitialize w DLL";
        lib.unload();
        return devices;
    }

    // Sprawdź kanały 1-8 przez próbę inicjalizacji
    static constexpr TPCANHandle channels[] = {
        PCAN_CHANNEL_USBBUS1, PCAN_CHANNEL_USBBUS2,
        PCAN_CHANNEL_USBBUS3, PCAN_CHANNEL_USBBUS4,
        PCAN_CHANNEL_USBBUS5, PCAN_CHANNEL_USBBUS6,
        PCAN_CHANNEL_USBBUS7, PCAN_CHANNEL_USBBUS8
    };
    static const char *names[] = {
        "PCAN_USBBUS1", "PCAN_USBBUS2", "PCAN_USBBUS3", "PCAN_USBBUS4",
        "PCAN_USBBUS5", "PCAN_USBBUS6", "PCAN_USBBUS7", "PCAN_USBBUS8"
    };

    for (int i = 0; i < 8; ++i) {
        int res = fnInit(channels[i], PCAN_BAUD_500K, 0, 0);
        if (res == 0) {
            // Hardware present — natychmiast zamknij
            fnUninit(channels[i]);
            devices.append(names[i]);
            qDebug() << "PcanDriver: wykryto sprzęt na" << names[i];
        } else if (res == 0x200) {  // NODRIVER
            // brak sterownika — pomiń
            qDebug() << "PcanDriver:" << names[i] << "— brak sterownika (NODRIVER)";
        } else if (res == 0x400) {  // HWINUSE
            devices.append(QString("%1 [Zajęty]").arg(names[i]));
            fnUninit(channels[i]);
            qDebug() << "PcanDriver:" << names[i] << "— sprzęt zajęty (HWINUSE)";
        } else if (res == 0xE00) {  // ILLHW
            // brak fizycznego sprzętu — pomiń
        } else {
            qDebug() << "PcanDriver:" << names[i] << "— kod błędu" << res;
        }
    }

    lib.unload();
    qDebug() << "PcanDriver::availableDevices: znaleziono" << devices.size() << "kanał(ów)";
    return devices;
}

#else
// ═══════════════════════════════════════════════════════════════
// Stub Linux – PcanDriver nie działa bez PCANBasic
// ═══════════════════════════════════════════════════════════════
struct PcanDriver::Impl {};

PcanDriver::PcanDriver()  { d = new Impl; }
PcanDriver::~PcanDriver() { delete d; }
bool PcanDriver::open(const QString &) { return false; }
void PcanDriver::close() {}
CanFrame PcanDriver::readFrame() { return {}; }
bool PcanDriver::isValid() const { return false; }
void PcanDriver::writeFrame(const CanFrame &) {}
QStringList PcanDriver::availableDevices() const { return {}; }
void PcanDriver::setBaudRate(const QString &) {}
#endif
