#include "ArxmlParser.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QHash>
#include <QVector>
#include <cmath>

// ── Helpers ──────────────────────────────────────────────────────────────────

static QString shortRef(const QString &ref) {
    int idx = ref.lastIndexOf('/');
    return (idx >= 0) ? ref.mid(idx + 1) : ref;
}

// ── Internal data structures ──────────────────────────────────────────────────

struct ISignalDef {
    int     bitLen   = 1;
    bool    bigEndian = false;   // MOST-SIGNIFICANT-BYTE-FIRST
    double  scale    = 1.0;
    double  offset   = 0.0;
    double  minimum  = 0.0;
    double  maximum  = 0.0;
    QString unit;
};

struct SigMapping {
    QString signalShortName;
    int     startPos  = 0;
    bool    bigEndian = false;
};

struct PduDef {
    int                  dlcBytes = 8;
    QVector<SigMapping>  mappings;
};

struct FrameDef {
    int             frameLen = 8;
    QVector<QString> pduRefs;    // short names of PDUs
};

// ── Parser ────────────────────────────────────────────────────────────────────

QVector<DbcMessage> ArxmlParser::load(const QString &path) {
    m_error.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = "Cannot open file: " + path;
        return {};
    }

    QXmlStreamReader xml(&file);

    QHash<QString, ISignalDef>  sigDefs;   // short-name → def
    QHash<QString, PduDef>      pdus;      // short-name → def
    QHash<QString, FrameDef>    frames;    // short-name → def

    // {canId, frameShortName}
    struct Trig { uint32_t id; QString frameShortName; };
    QVector<Trig> triggers;

    // ── Parsing state ────────────────────────────────────────────────────────
    QStringList path_stack;           // element name stack
    QString currentName;              // SHORT-NAME of current element
    bool    inSignal      = false;
    bool    inPdu         = false;
    bool    inFrame       = false;
    bool    inTrig        = false;
    bool    inSigMapping  = false;
    bool    inCompuMethod = false;

    // Current objects being built
    ISignalDef  curSig;
    PduDef      curPdu;
    FrameDef    curFrame;
    SigMapping  curMap;
    Trig        curTrig;
    QString     compuNumerator;
    QString     compuDenominator;

    // Whether we're in a COMPU-NUMERATOR / COMPU-DENOMINATOR
    bool inCompuNum = false, inCompuDen = false;
    bool inByteOrder = false;
    bool inMapByteOrder = false;

    auto path_contains = [&](const QString &tag) {
        return path_stack.contains(tag);
    };
    auto path_ends_with = [&](const QString &tag) {
        return !path_stack.isEmpty() && path_stack.last() == tag;
    };

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            const QString tag = xml.name().toString();
            path_stack.push_back(tag);

            if (tag == "I-SIGNAL" && !path_contains("I-SIGNAL-I-PDU")) {
                inSignal = true; curSig = {}; currentName.clear();
            } else if (tag == "I-SIGNAL-I-PDU") {
                inPdu = true; curPdu = {}; currentName.clear();
            } else if (tag == "FRAME") {
                inFrame = true; curFrame = {}; currentName.clear();
            } else if (tag == "CAN-FRAME-TRIGGERING") {
                inTrig = true; curTrig = {}; currentName.clear();
            } else if (tag == "I-SIGNAL-TO-I-PDU-MAPPING" || tag == "I-SIGNAL-TO-PDU-MAPPING") {
                inSigMapping = true; curMap = {};
            } else if (tag == "COMPU-METHOD") {
                inCompuMethod = true; compuNumerator.clear(); compuDenominator.clear();
            } else if (tag == "COMPU-NUMERATOR")   { inCompuNum = true; }
            else if (tag == "COMPU-DENOMINATOR")   { inCompuDen = true; }
            else if (tag == "PACKING-BYTE-ORDER")  { inMapByteOrder = true; }
            else if (tag == "BYTE-ORDER")          { inByteOrder = true; }

        } else if (xml.isEndElement()) {
            const QString tag = xml.name().toString();

            if (tag == "I-SIGNAL" && inSignal && !path_contains("I-SIGNAL-I-PDU")) {
                if (!currentName.isEmpty()) sigDefs[currentName] = curSig;
                inSignal = false;
            } else if (tag == "I-SIGNAL-I-PDU" && inPdu) {
                if (!currentName.isEmpty()) pdus[currentName] = curPdu;
                inPdu = false;
            } else if (tag == "FRAME" && inFrame) {
                if (!currentName.isEmpty()) frames[currentName] = curFrame;
                inFrame = false;
            } else if (tag == "CAN-FRAME-TRIGGERING" && inTrig) {
                triggers.push_back(curTrig);
                inTrig = false;
            } else if ((tag == "I-SIGNAL-TO-I-PDU-MAPPING" || tag == "I-SIGNAL-TO-PDU-MAPPING") && inSigMapping) {
                curPdu.mappings.push_back(curMap);
                inSigMapping = false;
            } else if (tag == "COMPU-METHOD" && inCompuMethod) {
                inCompuMethod = false;
            } else if (tag == "COMPU-NUMERATOR")  { inCompuNum = false; }
            else if (tag == "COMPU-DENOMINATOR")  { inCompuDen = false; }
            else if (tag == "PACKING-BYTE-ORDER") { inMapByteOrder = false; }
            else if (tag == "BYTE-ORDER")         { inByteOrder = false; }

            if (!path_stack.isEmpty()) path_stack.pop_back();

        } else if (xml.isCharacters() && !xml.isWhitespace()) {
            const QString val = xml.text().toString().trimmed();
            const QString ctx = path_stack.isEmpty() ? "" : path_stack.last();

            // SHORT-NAME – used by the current outer element
            if (ctx == "SHORT-NAME") {
                currentName = val;
                if (inSignal && !path_contains("I-SIGNAL-I-PDU"))
                    ; // will be used at end of I-SIGNAL
                if (inTrig)
                    curTrig.frameShortName.clear(); // will be overwritten later if needed
            }

            // Signal: bit length
            if (inSignal && !inSigMapping && ctx == "BIT-LENGTH")
                curSig.bitLen = val.toInt();

            // Signal: byte order
            if (inSignal && !inSigMapping && inByteOrder)
                curSig.bigEndian = val.contains("MOST-SIGNIFICANT-BYTE-FIRST");

            // PDU: length
            if (inPdu && ctx == "LENGTH")
                curPdu.dlcBytes = val.toInt();

            // Mapping: signal ref
            if (inSigMapping && (ctx == "I-SIGNAL-REF"))
                curMap.signalShortName = shortRef(val);

            // Mapping: start position
            if (inSigMapping && ctx == "START-POSITION")
                curMap.startPos = val.toInt();

            // Mapping: byte order
            if (inSigMapping && inMapByteOrder)
                curMap.bigEndian = val.contains("MOST-SIGNIFICANT-BYTE-FIRST");

            // Frame: length
            if (inFrame && ctx == "FRAME-LENGTH")
                curFrame.frameLen = val.toInt();

            // Frame: PDU ref
            if (inFrame && ctx == "PDU-REF")
                curFrame.pduRefs.append(shortRef(val));

            // Trig: identifier
            if (inTrig && ctx == "IDENTIFIER") {
                bool ok;
                curTrig.id = val.toUInt(&ok, 0);
            }

            // Trig: frame ref
            if (inTrig && ctx == "FRAME-REF")
                curTrig.frameShortName = shortRef(val);

            // SHORT-NAME for trig
            if (inTrig && ctx == "SHORT-NAME" && curTrig.frameShortName.isEmpty())
                ;  // trig's own short name, ignore

            // COMPU-METHOD: scale
            if (inCompuMethod && inCompuNum && ctx == "V")
                compuNumerator = val;
            if (inCompuMethod && inCompuDen && ctx == "V")
                compuDenominator = val;
        }
    }

    if (xml.hasError()) {
        m_error = QString("XML parse error at line %1: %2")
            .arg(xml.lineNumber()).arg(xml.errorString());
    }

    // ── Assemble DbcMessages ─────────────────────────────────────────────────
    QVector<DbcMessage> result;

    for (const auto &trig : triggers) {
        DbcMessage msg;
        msg.id   = trig.id;
        msg.name = trig.frameShortName;
        msg.dlc  = 8;

        auto frameIt = frames.find(trig.frameShortName);
        if (frameIt != frames.end()) {
            msg.dlc = frameIt->frameLen;
            msg.name = trig.frameShortName;

            for (const QString &pduRef : frameIt->pduRefs) {
                auto pduIt = pdus.find(pduRef);
                if (pduIt == pdus.end()) continue;

                for (const auto &mapping : pduIt->mappings) {
                    auto sigIt = sigDefs.find(mapping.signalShortName);

                    DbcSignal sig;
                    sig.name          = mapping.signalShortName;
                    sig.startBit      = mapping.startPos;
                    sig.length        = (sigIt != sigDefs.end()) ? sigIt->bitLen : 8;
                    sig.isLittleEndian = !(mapping.bigEndian || (sigIt != sigDefs.end() && sigIt->bigEndian));
                    sig.isSigned      = false;
                    sig.scale         = (sigIt != sigDefs.end()) ? sigIt->scale  : 1.0;
                    sig.offset        = (sigIt != sigDefs.end()) ? sigIt->offset : 0.0;
                    sig.minimum       = (sigIt != sigDefs.end()) ? sigIt->minimum : 0.0;
                    sig.maximum       = (sigIt != sigDefs.end()) ? sigIt->maximum
                                      : (std::pow(2.0, sig.length) - 1.0) * sig.scale + sig.offset;
                    sig.unit          = (sigIt != sigDefs.end()) ? sigIt->unit : "";

                    msg.sigList.append(sig);
                }
            }
        }

        if (msg.sigList.isEmpty() && frameIt == frames.end()) {
            // Minimal stub: no frame info found, skip
            continue;
        }
        result.append(msg);
    }

    return result;
}
