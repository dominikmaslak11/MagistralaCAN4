#pragma once
#include "CanReplayFilter.h"
#include "CanSniffer.h"
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QComboBox>

/// Widget for editing CAN replay transformation rules and injecting modified frames.
class CanReplayFilterWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanReplayFilterWidget(CanSniffer *sniffer, QWidget *parent = nullptr);

    /// Apply rules to a batch and inject results into the sniffer.
    void replayBatch(const std::vector<CanFrame> &frames);

public slots:
    void addRule();
    void removeSelectedRule();
    void applyRules();
    void clearRules();

private:
    void buildUi();
    ReplayRule ruleFromRow(int row) const;
    void refreshRuleTable();

    CanReplayFilter m_filter;
    CanSniffer     *m_sniffer;

    // Rule editor
    QLineEdit      *m_srcIdEdit{};
    QLineEdit      *m_dstIdEdit{};
    QCheckBox      *m_remapCheck{};
    QCheckBox      *m_dropCheck{};

    // Byte transform editors (per-byte scale/offset)
    QDoubleSpinBox *m_scaleSpins[8]{};
    QDoubleSpinBox *m_offsetSpins[8]{};
    QCheckBox      *m_byteActiveChecks[8]{};

    QPushButton    *m_addBtn{};
    QPushButton    *m_removeBtn{};
    QPushButton    *m_applyBtn{};
    QPushButton    *m_clearBtn{};

    QTableWidget   *m_rulesTable{};
    QLabel         *m_statusLabel{};
};
