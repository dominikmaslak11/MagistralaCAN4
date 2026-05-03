#pragma once
#include <QVector>
#include <QString>

/**
 * Klasa wykonująca obliczenia korelacji na GPU (OpenCL) lub CPU.
 * Wejście: lista wektorów cech (każdy wektor odpowiada jednemu identyfikatorowi CAN)
 * Wyjście: macierz podobieństw między wszystkimi parami wektorów.
 */
class GpuCorrelator {
public:
    GpuCorrelator();
    ~GpuCorrelator();

    bool initialize();  // zwraca true, jeśli udało się załadować OpenCL
    bool isGpuAvailable() const { return m_gpuAvailable; }

    // Oblicza macierz korelacji (N x N) – każdy element to współczynnik podobieństwa (0..1)
    // Wektory wejściowe muszą być tej samej długości.
    QVector<QVector<float>> computeCorrelationMatrix(const QVector<QVector<float>> &features);

private:
    bool m_gpuAvailable = false;
    // uchwyty OpenCL – szczegóły w .cpp
    void *m_context = nullptr;
    void *m_queue = nullptr;
    void *m_program = nullptr;
    void *m_kernel = nullptr;

    QVector<QVector<float>> computeOnCpu(const QVector<QVector<float>> &features);
    QVector<QVector<float>> computeOnGpu(const QVector<QVector<float>> &features);
};
