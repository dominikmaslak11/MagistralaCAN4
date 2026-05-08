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
#define PCAN_BAUD_500K           0x001C
#define PCAN_MESSAGE_STANDARD    0x00
#define PCAN_MESSAGE_EXTENDED    0x02
#define PCAN_MESSAGE_FD          0x10
#define PCAN_MESSAGE_RTR         0x01

typedef struct { DWORD ID; BYTE MSGTYPE; BYTE LEN; BYTE DATA[8]; } TPCANMsg;
typedef struct { DWORD ID; DWORD MSGTYPE; DWORD DLC; BYTE DATA[64]; } TPCANMsgFD;
typedef ULONGLONG TPCANTimestamp;
typedef BYTE TPCANHandle;

struct PcanDriver::Impl {
    QLibrary lib;
    TPCANHandle channel = 0;
    bool initialized = false;

    typedef int (__stdcall *CAN_Init_t)(TPCANHandle, WORD, DWORD, DWORD);
    typedef int (__stdcall *CAN_Uninit_t)(TPCANHandle);
    typedef int (__stdcall *CAN_Read_t)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
    typedef int (__stdcall *CAN_ReadFD_t)(TPCANHandle, TPCANMsgFD*, TPCANTimestamp*);
    typedef int (__stdcall *CAN_Write_t)(TPCANHandle, TPCANMsg*);
    typedef int (__stdcall *CAN_WriteFD_t)(TPCANHandle, TPCANMsgFD*);
    typedef int (__stdcall *CAN_GetStatus_t)(TPCANHandle);
    typedef int (__stdcall *CAN_GetErr_t)(int, WORD, char*);

    CAN_Init_t      fnInit   = nullptr;
    CAN_Uninit_t    fnUninit = nullptr;
    CAN_Read_t      fnRead   = nullptr;
    CAN_ReadFD_t    fnReadFD = nullptr;
    CAN_Write_t     fnWrite  = nullptr;
    CAN_WriteFD_t   fnWriteFD= nullptr;
    CAN_GetStatus_t fnStatus = nullptr;
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
    d->fnStatus = (Impl::CAN_GetStatus_t)d->lib.resolve("CAN_GetStatus");
    d->fnErrTxt = (Impl::CAN_GetErr_t)  d->lib.resolve("CAN_GetErrorText");
    if (!d->fnInit || !d->fnRead || !d->fnWrite) {
        qWarning() << "PcanDriver: brak wymaganych funkcji w PCANBasic.dll";
        d->lib.unload();
        return false;
    }

    //  Wybierz kanał
    if (device == "PCAN_USBBUS2") d->channel = PCAN_CHANNEL_USBBUS2;
    else if (device == "PCAN_USBBUS3") d->channel = PCAN_CHANNEL_USBBUS3;
    else d->channel = PCAN_CHANNEL_USBBUS1;

    int res = d->fnInit(d->channel, PCAN_BAUD_500K, 0, 0);
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

    //  Sprawdź czy kolejka nie jest pusta
    if (d->fnStatus) {
        int st = d->fnStatus(d->channel);
        if (st & 0x100) return {}; // brak danych
    }

    //  FD
    if (d->fnReadFD) {
        TPCANMsgFD msg; memset(&msg, 0, sizeof(msg));
        TPCANTimestamp ts = 0;
        if (d->fnReadFD(d->channel, &msg, &ts) == 0) {
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

    //  Klasyczne CAN
    if (d->fnRead) {
        TPCANMsg msg; memset(&msg, 0, sizeof(msg));
        TPCANTimestamp ts = 0;
        if (d->fnRead(d->channel, &msg, &ts) == 0) {
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

QStringList PcanDriver::availableDevices() const {
    return {"PCAN_USBBUS1", "PCAN_USBBUS2", "PCAN_USBBUS3"};
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
#endif
