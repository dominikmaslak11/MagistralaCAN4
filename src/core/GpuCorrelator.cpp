#include "GpuCorrelator.h"
#include <QDebug>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

#ifdef HAS_OPENCL
#include <CL/cl.h>
#endif

GpuCorrelator::GpuCorrelator() {
    initialize();
}

GpuCorrelator::~GpuCorrelator() {
#ifdef HAS_OPENCL
    if (m_kernel) clReleaseKernel(static_cast<cl_kernel>(m_kernel));
    if (m_program) clReleaseProgram(static_cast<cl_program>(m_program));
    if (m_queue) clReleaseCommandQueue(static_cast<cl_command_queue>(m_queue));
    if (m_context) clReleaseContext(static_cast<cl_context>(m_context));
#endif
}

bool GpuCorrelator::initialize() {
#ifdef HAS_OPENCL
    cl_int err;
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;

    err = clGetPlatformIDs(1, &platform, nullptr);
    if (err != CL_SUCCESS) {
        qWarning("GpuCorrelator: brak platformy OpenCL.");
        return false;
    }

    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
    if (err != CL_SUCCESS) {
        qWarning("GpuCorrelator: brak urządzenia GPU, spróbuję CPU.");
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, nullptr);
        if (err != CL_SUCCESS) {
            qWarning("GpuCorrelator: brak jakiegokolwiek urządzenia OpenCL.");
            return false;
        }
    }

    cl_context_properties props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
    cl_context ctx = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) return false;
    m_context = ctx;

    cl_command_queue queue = clCreateCommandQueueWithProperties(ctx, device, nullptr, &err);
    if (err != CL_SUCCESS) { clReleaseContext(ctx); return false; }
    m_queue = queue;

    const char *kernelSource = R"CLC(
        __kernel void correlation(__global const float *features,
                                  __global float *result,
                                  const int N,
                                  const int vecLen) {
            int row = get_global_id(0);
            int col = get_global_id(1);
            if (row >= N || col >= N) return;
            float dot = 0.0f, normA = 0.0f, normB = 0.0f;
            for (int i = 0; i < vecLen; i++) {
                float a = features[row * vecLen + i];
                float b = features[col * vecLen + i];
                dot += a * b;
                normA += a * a;
                normB += b * b;
            }
            result[row * N + col] = dot / (sqrt(normA) * sqrt(normB) + 1e-6f);
        }
    )CLC";

    cl_program program = clCreateProgramWithSource(ctx, 1, &kernelSource, nullptr, &err);
    if (err != CL_SUCCESS) {
        clReleaseCommandQueue(queue); clReleaseContext(ctx); return false;
    }
    err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t len;
        char buffer[2048];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        qWarning() << "Błąd budowania kernela OpenCL:" << buffer;
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(ctx);
        return false;
    }
    m_program = program;

    cl_kernel kernel = clCreateKernel(program, "correlation", &err);
    if (err != CL_SUCCESS) {
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(ctx);
        return false;
    }
    m_kernel = kernel;
    m_gpuAvailable = true;
    qDebug("GpuCorrelator: OpenCL gotowy.");
#endif
    return m_gpuAvailable;
}

QVector<QVector<float>> GpuCorrelator::computeCorrelationMatrix(const QVector<QVector<float>> &features) {
#ifdef HAS_OPENCL
    if (m_gpuAvailable && !features.isEmpty())
        return computeOnGpu(features);
#endif
    return computeOnCpu(features);
}

QVector<QVector<float>> GpuCorrelator::computeOnCpu(const QVector<QVector<float>> &features) {
    int N = static_cast<int>(features.size());
    QVector<QVector<float>> result(N, QVector<float>(N, 0.0f));
    if (N == 0) return result;
    int vecLen = static_cast<int>(features[0].size());

    QVector<int> rows(N);
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int i) {
        for (int j = 0; j < N; ++j) {
            float dot = 0.0f, normA = 0.0f, normB = 0.0f;
            for (int k = 0; k < vecLen; ++k) {
                float a = features[i][k];
                float b = features[j][k];
                dot += a * b;
                normA += a * a;
                normB += b * b;
            }
            result[i][j] = dot / (std::sqrt(normA) * std::sqrt(normB) + 1e-6f);
        }
    });
    return result;
}

#ifdef HAS_OPENCL
QVector<QVector<float>> GpuCorrelator::computeOnGpu(const QVector<QVector<float>> &features) {
    int N = static_cast<int>(features.size());
    if (N == 0) return {};
    int vecLen = static_cast<int>(features[0].size());

    QVector<float> flatFeatures(N * vecLen);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < vecLen; ++j)
            flatFeatures[i * vecLen + j] = features[i][j];

    cl_int err;
    cl_context ctx = static_cast<cl_context>(m_context);
    cl_command_queue queue = static_cast<cl_command_queue>(m_queue);
    cl_kernel kernel = static_cast<cl_kernel>(m_kernel);

    cl_mem bufFeatures = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        flatFeatures.size() * sizeof(float),
                                        const_cast<float*>(flatFeatures.data()), &err);
    cl_mem bufResult = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                      N * N * sizeof(float), nullptr, &err);

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &bufFeatures);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &bufResult);
    clSetKernelArg(kernel, 2, sizeof(int), &N);
    clSetKernelArg(kernel, 3, sizeof(int), &vecLen);

    size_t global[2] = { static_cast<size_t>(N), static_cast<size_t>(N) };
    clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);

    QVector<float> resultFlat(N * N);
    clEnqueueReadBuffer(queue, bufResult, CL_TRUE, 0, N * N * sizeof(float), resultFlat.data(), 0, nullptr, nullptr);

    clReleaseMemObject(bufFeatures);
    clReleaseMemObject(bufResult);

    QVector<QVector<float>> result(N, QVector<float>(N));
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            result[i][j] = resultFlat[i * N + j];
    return result;
}
#endif
