#pragma once
#include "CanFrame.h"
#include "DbcParser.h"
#include <QHash>
#include <QString>
#include <cstdint>
#include <cmath>

/// Encodes physical signal values into CAN frame data bytes.
///
/// This is the inverse operation of DbcParser::decodeSignals().
/// Supports both little-endian (Intel) and big-endian (Motorola) bit packing.
class CanSignalEncoder {
public:
    /// Compute raw integer from physical value: raw = round((phys - offset) / scale).
    /// Clamps result to the representable range for the signal's bit length.
    static int64_t physToRaw(const DbcSignal &sig, double physValue);

    /// Pack a single signal's raw value into data[0..dlc-1].
    /// Returns false if the signal's bits extend beyond dlc bytes.
    static bool encodeSignal(const DbcSignal &sig, double physValue,
                             uint8_t *data, int dlc);

    /// Build a complete CanFrame from a DBC message and a map of physical values.
    /// Signals not present in 'values' are left at zero.
    /// @param canId  Override frame CAN ID (0 = use msg.id).
    static CanFrame buildFrame(const DbcMessage &msg,
                               const QHash<QString, double> &values,
                               uint32_t canId = 0);
};
