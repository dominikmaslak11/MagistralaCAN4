#pragma once
#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QFutureWatcher>
#include "CanFrame.h"

class CanFrameModel;

/**
 * @brief Panel eksportu ramek CAN — CSV, candump, PCAP, MCAN.
 *
 * Obsługuje filtrowanie po zakresie ID i oknie czasowym.
 * Eksport przebiega asynchronicznie (QtConcurrent) bez blokowania GUI.
 */
class CanExporter : public QWidget {
    Q_OBJECT
public:
    explicit CanExporter(CanFrameModel *model, QWidget *parent = nullptr);

    enum class Format { CSV, Candump, Pcap, Mcan };

    /// Eksportuje wskazany wektor ramek do pliku (do testów i eksportu zewnętrznego).
    static bool exportCsv(const QString &path, const QVector<CanFrame> &frames);
    static bool exportCandump(const QString &path, const QVector<CanFrame> &frames,
                              const QString &iface = "vcan0");
    static bool exportPcap(const QString &path, const QVector<CanFrame> &frames);
    static bool exportMcan(const QString &path, const QVector<CanFrame> &frames);

private slots:
    void onExport();
    void onBrowse();
    void onFilterChanged();
    void onExportFinished();

private:
    void setupUi();
    QVector<CanFrame> filteredFrames() const;
    QString defaultExtension() const;

    CanFrameModel *m_model;

    // UI
    QComboBox   *m_formatCombo{};
    QLineEdit   *m_pathEdit{};
    QPushButton *m_browseBtn{};
    QPushButton *m_exportBtn{};
    QProgressBar *m_progress{};

    // Filtry
    QLineEdit   *m_idMinEdit{};
    QLineEdit   *m_idMaxEdit{};
    QCheckBox   *m_timeFilterCheck{};
    QLineEdit   *m_timeFromEdit{};
    QLineEdit   *m_timeToEdit{};

    // Status
    QLabel *m_statsLabel{};
    QLabel *m_resultLabel{};

    QFutureWatcher<bool> *m_watcher = nullptr;
};
