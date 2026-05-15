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
#include "ObdFrame.h"

class ObdTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { TIME, CAN_ID, DIR, MODE, MODE_NAME, PID, PID_NAME, VALUE, _COUNT };
    explicit ObdTableModel(QObject *p = nullptr);
    int rowCount(const QModelIndex & = QModelIndex()) const override { return m_frames.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return _COUNT; }
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

    ObdParser       m_parser;
    ObdTableModel  *m_model     = nullptr;
    QTableView     *m_table     = nullptr;
    QTableWidget   *m_dtcTable  = nullptr;
    QTableWidget   *m_liveTable = nullptr;
    QLabel         *m_statusLabel = nullptr;
    QPushButton    *m_clearBtn    = nullptr;

    // PID → {name, value, unit} for live panel
    struct LiveEntry { QString name; double value; QString unit; };
    QHash<uint16_t, LiveEntry> m_liveValues;
    int m_totalFrames = 0;
};
