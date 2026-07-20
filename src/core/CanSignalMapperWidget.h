#pragma once
#include <QWidget>
#include "CanSignalMapperModel.h"

class QTableView;

class CanSignalMapperWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanSignalMapperWidget(QWidget *parent = nullptr);

public slots:
    void processFrame(const CanFrame &frame);

private slots:
    void addSignalDialog();
    void removeSelected();
    void saveToFile();
    void loadFromFile();

private:
    void setupUi();

    CanSignalMapperModel *m_model;
    QTableView           *m_table{};
};
