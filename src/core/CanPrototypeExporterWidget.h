#pragma once
#include "CanPrototypeExporter.h"
#include "CanModuleProfiler.h"
#include "CanFrame.h"
#include <QWidget>
#include <QVector>
#include <deque>
#include <unordered_map>

class CanFrameModel;
class DbcParser;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QTextEdit;
class QLabel;
class QCheckBox;

class CanPrototypeExporterWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanPrototypeExporterWidget(QWidget *parent = nullptr);

    void setFrameModel (CanFrameModel *model)  { m_model     = model;  }
    void setDbcParser  (DbcParser     *parser) { m_dbcParser = parser; }
    void setProfiles   (const QVector<ModuleProfile> &profiles) { m_profiles = profiles; }

public slots:
    void onFrame(const CanFrame &frame, uint64_t tsUs = 0);

private slots:
    void onGenerate();
    void onExport();
    void onFormatChanged(int index);

private:
    QVector<FrameExportInfo> buildExportInfo() const;
    QVector<int> detectCounterBytes(uint32_t id) const;

    CanFrameModel          *m_model     = nullptr;
    DbcParser              *m_dbcParser = nullptr;
    QVector<ModuleProfile>  m_profiles;

    // rolling frame history: last kMaxHistory frames per CAN ID
    static constexpr int kMaxHistory = 64;
    std::unordered_map<uint32_t, std::deque<CanFrame>> m_history;

    // UI
    QComboBox   *m_formatCombo;
    QComboBox   *m_busTypeCombo;
    QLineEdit   *m_channelEdit;
    QSpinBox    *m_bitrateBox;
    QCheckBox   *m_rxCheck;
    QPushButton *m_generateBtn;
    QPushButton *m_exportBtn;
    QTextEdit   *m_codeView;
    QLabel      *m_statsLabel;
};
