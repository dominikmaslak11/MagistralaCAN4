#pragma once
#include <QWidget>
#include <QTableView>
#include <QTableWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <QPainter>
#include "ObdFrame.h"
#include "IsoTpReassembler.h"

// Arc-style gauge widget for a single OBD-II PID value.
class ObdGauge : public QWidget {
    Q_OBJECT
public:
    ObdGauge(const QString &name, uint16_t pid,
             const QString &unit, double min, double max,
             QWidget *parent = nullptr);

    void setValue(double v);
    uint16_t pid() const { return m_pid; }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override { return {160, 145}; }
    QSize minimumSizeHint() const override { return {120, 110}; }

private:
    QString  m_name, m_unit;
    uint16_t m_pid;
    double   m_min, m_max, m_value = 0;
    bool     m_hasValue = false;
};

// ── ObdTableModel ──────────────────────────────────────────────────────────────

class ObdTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, DIR, MODE, MODE_NAME, PID, PID_NAME, VALUE, Count };
    explicit ObdTableModel(QObject *p = nullptr);
    int rowCount(const QModelIndex & = QModelIndex()) const override { return m_frames.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return Count; }
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int s, Qt::Orientation o, int role) const override;
    void addFrame(const ObdFrame &f);
    void clear();
    void setParser(const ObdParser *p) { m_parser = p; }
private:
    QVector<ObdFrame> m_frames;
    const ObdParser *m_parser = nullptr;
    static constexpr int MAX = 2000;
};

// ── ObdWidget ──────────────────────────────────────────────────────────────────

class ObdWidget : public QWidget {
    Q_OBJECT
public:
    explicit ObdWidget(QWidget *parent = nullptr);
public slots:
    void processFrame(const CanFrame &frame);
private:
    void setupUi();
    void updateDtcTable(const QStringList &dtcs, uint8_t mode);
    void updateLivePidTable(const ObdFrame &f);
    void updateGauges(const ObdFrame &f);

    ObdParser          m_parser;
    IsoTpReassembler   m_reassembler;
    ObdTableModel     *m_model      = nullptr;
    QTableView        *m_table      = nullptr;
    QTableWidget      *m_dtcTable   = nullptr;
    QTableWidget      *m_liveTable  = nullptr;
    QLabel            *m_statusLabel = nullptr;
    QPushButton       *m_clearBtn   = nullptr;

    // PID → {name, value, unit} for live panel
    struct LiveEntry { QString name; double value{}; QString unit; };
    QHash<uint16_t, LiveEntry> m_liveValues;
    int m_totalFrames = 0;

    // Arc gauges for the most-watched Mode 01 PIDs
    static constexpr int GAUGE_COUNT = 6;
    ObdGauge *m_gauges[GAUGE_COUNT] = {};
};
