#pragma once
#include <QVector>
#include <QString>

class GpuCorrelator {
public:
    GpuCorrelator();
    ~GpuCorrelator();

    bool initialize();
    bool isGpuAvailable() const { return m_gpuAvailable; }

    QVector<QVector<float>> computeCorrelationMatrix(const QVector<QVector<float>> &features);

private:
    bool m_gpuAvailable = false;
    void *m_context = nullptr;
    void *m_queue = nullptr;
    void *m_program = nullptr;
    void *m_kernel = nullptr;

    QVector<QVector<float>> computeOnCpu(const QVector<QVector<float>> &features);
#ifdef HAS_OPENCL
    QVector<QVector<float>> computeOnGpu(const QVector<QVector<float>> &features);
#endif
};
