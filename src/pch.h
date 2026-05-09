#pragma once
// ═══════════════════════════════════════════════════════════════
// Precompiled header — MagistralaCAN4
// Stabilne, ciężkie nagłówki STL + Qt (bez nagłówków projektu).
// Wszystkie 46 translation units korzysta z tego PCH przez CMake
// target_precompile_headers → /FI (MSVC) / -include (GCC).
// ═══════════════════════════════════════════════════════════════

// ── Standard library ──
#include <cstdint>
#include <cstring>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <vector>
#include <deque>
#include <array>
#include <utility>
#include <complex>
#include <random>
#include <atomic>

// ── Qt Core ──
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QPair>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <QRegularExpression>
#include <QMetaType>
#include <QDataStream>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ── Qt GUI / Widgets ──
#include <QApplication>
#include <QWidget>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QTableView>
#include <QTableWidget>
#include <QListWidget>
#include <QAbstractTableModel>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QGroupBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QShortcut>
#include <QPainter>
#include <QFontMetrics>
#include <QSurfaceFormat>
#include <QIcon>
#include <QColor>
#include <QEvent>

// ── Qt Charts (template-heavy) ──
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>

// ── OpenGL ──
#include <QOpenGLWidget>
