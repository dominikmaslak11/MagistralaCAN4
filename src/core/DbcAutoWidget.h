#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QSpinBox>
#include <QProgressBar>
#include <vector>
#include "CanFrame.h"
#include "DbcAutoGenerator.h"

class DbcParser;
class DbcEditorWidget;

class DbcAutoWidget : public QWidget {
    Q_OBJECT
public:
    explicit DbcAutoWidget(QWidget *parent = nullptr);

    // Allow external code to inject a DBC editor for "Apply to editor" workflow
    void setDbcEditor(DbcEditorWidget *editor) { m_dbcEditor = editor; }

public slots:
    // Feed frames (throttled in MainWindow) — accumulates up to m_maxFrames
    void processFrame(const CanFrame &frame);
    void clearCapture();
    void runAnalysis();
    void exportDbc();
    void applyToEditor();

private slots:
    void onMessageSelected(int row);

private:
    void populateTable(const std::vector<AutoMessage> &msgs);
    void showMessageDetail(const AutoMessage &msg);

    // Captured frames (ring, capped)
    std::vector<CanFrame> m_frames;
    int m_maxFrames = 100000;

    // Last analysis result
    std::vector<AutoMessage> m_lastResult;

    DbcEditorWidget *m_dbcEditor = nullptr;

    // ── Widgets ────────────────────────────────────────────────
    QLabel       *m_captureLabel;
    QSpinBox     *m_minFramesSpin;
    QPushButton  *m_analyzeBtn;
    QPushButton  *m_clearBtn;
    QPushButton  *m_exportBtn;
    QPushButton  *m_applyBtn;
    QProgressBar *m_progressBar;
    QTableWidget *m_msgTable;
    QTextEdit    *m_detailView;
    QLabel       *m_statusLabel;
};
