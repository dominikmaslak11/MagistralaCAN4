#include "GpuCompute.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

#ifdef HAS_OPENCL
#include <CL/cl.h>
#endif

GpuCompute::GpuCompute() {
    m_available = initOpenCL();
}

GpuCompute::~GpuCompute() {
#ifdef HAS_OPENCL
    if (m_kernKmeans) clReleaseKernel(static_cast<cl_kernel>(m_kernKmeans));
    if (m_progKmeans) clReleaseProgram(static_cast<cl_program>(m_progKmeans));
    if (m_kernPearson) clReleaseKernel(static_cast<cl_kernel>(m_kernPearson));
    if (m_progPearson) clReleaseProgram(static_cast<cl_program>(m_progPearson));
    if (m_kernCorr) clReleaseKernel(static_cast<cl_kernel>(m_kernCorr));
    if (m_progCorr) clReleaseProgram(static_cast<cl_program>(m_progCorr));
    if (m_queue) clReleaseCommandQueue(static_cast<cl_command_queue>(m_queue));
    if (m_context) clReleaseContext(static_cast<cl_context>(m_context));
#endif
}

bool GpuCompute::initOpenCL() {
#ifdef HAS_OPENCL
    cl_int err;
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;

    err = clGetPlatformIDs(1, &platform, nullptr);
    if (err != CL_SUCCESS) return false;

    // Prefer GPU
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
    if (err != CL_SUCCESS) {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, nullptr);
        if (err != CL_SUCCESS) return false;
    }

    cl_context_properties props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
    cl_context ctx = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) return false;
    m_context = ctx;

    cl_command_queue queue = clCreateCommandQueueWithProperties(ctx, device, nullptr, &err);
    if (err != CL_SUCCESS) { clReleaseContext(ctx); return false; }
    m_queue = queue;

    // ── Kernel 1: correlation matrix ───────────────────────
    const char *corrSrc = R"CLC(
        __kernel void correlation(__global const float *features,
                                  __global float *result,
                                  const int N, const int vecLen) {
            int row = get_global_id(0), col = get_global_id(1);
            if (row >= N || col >= N) return;
            float dot = 0, nA = 0, nB = 0;
            for (int i = 0; i < vecLen; i++) {
                float a = features[row * vecLen + i];
                float b = features[col * vecLen + i];
                dot += a * b; nA += a * a; nB += b * b;
            }
            result[row * N + col] = dot / (sqrt(nA) * sqrt(nB) + 1e-6f);
        }
    )CLC";

    cl_program prog = clCreateProgramWithSource(ctx, 1, &corrSrc, nullptr, &err);
    err = clBuildProgram(prog, 1, &device, nullptr, nullptr, nullptr);
    if (err == CL_SUCCESS) {
        m_progCorr = prog;
        m_kernCorr = clCreateKernel(prog, "correlation", &err);
    } else { clReleaseProgram(prog); }

    // ── Kernel 2: batch Pearson ────────────────────────────
    const char *pearsonSrc = R"CLC(
        __kernel void pearson_batch(__global const float *xs,
                                    __global const float *ys,
                                    __global float *corrs,
                                    const int P, const int N) {
            int p = get_global_id(0);
            if (p >= P) return;
            float sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
            for (int i = 0; i < N; i++) {
                float x = xs[p * N + i];
                float y = ys[p * N + i];
                sx += x; sy += y; sxy += x * y; sx2 += x * x; sy2 += y * y;
            }
            float den = sqrt((N * sx2 - sx * sx) * (N * sy2 - sy * sy));
            corrs[p] = den > 0 ? (N * sxy - sx * sy) / den : 0;
        }
    )CLC";

    prog = clCreateProgramWithSource(ctx, 1, &pearsonSrc, nullptr, &err);
    err = clBuildProgram(prog, 1, &device, nullptr, nullptr, nullptr);
    if (err == CL_SUCCESS) {
        m_progPearson = prog;
        m_kernPearson = clCreateKernel(prog, "pearson_batch", &err);
    } else { clReleaseProgram(prog); }

    // ── Kernel 3: k-means distances ────────────────────────
    const char *kmeansSrc = R"CLC(
        __kernel void kmeans_dist(__global const float *points,
                                  __global const float *centroids,
                                  __global float *dists,
                                  const int N, const int K, const int dim) {
            int i = get_global_id(0), k = get_global_id(1);
            if (i >= N || k >= K) return;
            float d2 = 0;
            for (int d = 0; d < dim; d++) {
                float diff = points[i * dim + d] - centroids[k * dim + d];
                d2 += diff * diff;
            }
            dists[i * K + k] = d2;
        }
    )CLC";

    prog = clCreateProgramWithSource(ctx, 1, &kmeansSrc, nullptr, &err);
    err = clBuildProgram(prog, 1, &device, nullptr, nullptr, nullptr);
    if (err == CL_SUCCESS) {
        m_progKmeans = prog;
        m_kernKmeans = clCreateKernel(prog, "kmeans_dist", &err);
    } else { clReleaseProgram(prog); }

    return m_kernCorr != nullptr;  // at least correlation kernel works
#else
    return false;
#endif
}

void GpuCompute::cpuFallback(const char *op) {
    // Silent fallback — GPU unavailable
    (void)op;
}

// ── Correlation matrix ──────────────────────────────────────

std::vector<std::vector<float>>
GpuCompute::correlationMatrix(const std::vector<std::vector<float>> &features) {
    int N = static_cast<int>(features.size());
    std::vector<std::vector<float>> result(N, std::vector<float>(N, 0));
    if (N == 0) return result;
    int dim = static_cast<int>(features[0].size());

#ifdef HAS_OPENCL
    if (m_available && m_kernCorr && N > 10) {
        // Flatten
        std::vector<float> flat(N * dim);
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < dim; ++j)
                flat[i * dim + j] = features[i][j];

        cl_int err;
        cl_context ctx = static_cast<cl_context>(m_context);
        cl_command_queue q = static_cast<cl_command_queue>(m_queue);
        cl_kernel kern = static_cast<cl_kernel>(m_kernCorr);

        cl_mem bufF = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     flat.size() * sizeof(float), flat.data(), &err);
        cl_mem bufR = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                     N * N * sizeof(float), nullptr, &err);

        clSetKernelArg(kern, 0, sizeof(cl_mem), &bufF);
        clSetKernelArg(kern, 1, sizeof(cl_mem), &bufR);
        clSetKernelArg(kern, 2, sizeof(int), &N);
        clSetKernelArg(kern, 3, sizeof(int), &dim);

        size_t global[2] = { (size_t)N, (size_t)N };
        clEnqueueNDRangeKernel(q, kern, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        clFinish(q);

        std::vector<float> resFlat(N * N);
        clEnqueueReadBuffer(q, bufR, CL_TRUE, 0, N * N * sizeof(float), resFlat.data(), 0, nullptr, nullptr);
        clReleaseMemObject(bufF); clReleaseMemObject(bufR);

        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                result[i][j] = resFlat[i * N + j];
        return result;
    }
#endif
    // CPU fallback
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            float dot = 0, nA = 0, nB = 0;
            for (int k = 0; k < dim; ++k) {
                float a = features[i][k], b = features[j][k];
                dot += a * b; nA += a * a; nB += b * b;
            }
            result[i][j] = dot / (std::sqrt(nA) * std::sqrt(nB) + 1e-6f);
        }
    return result;
}

// ── Batch Pearson ───────────────────────────────────────────

std::vector<double>
GpuCompute::pearsonBatch(const std::vector<std::vector<double>> &xs,
                         const std::vector<std::vector<double>> &ys) {
    int P = static_cast<int>(xs.size());
    std::vector<double> result(P, 0);
    if (P == 0 || xs[0].empty()) return result;
    int N = static_cast<int>(xs[0].size());

#ifdef HAS_OPENCL
    if (m_available && m_kernPearson && P * N > 1000) {
        std::vector<float> flatX(P * N), flatY(P * N);
        for (int p = 0; p < P; ++p)
            for (int i = 0; i < N; ++i) {
                flatX[p * N + i] = (float)xs[p][i];
                flatY[p * N + i] = (float)ys[p][i];
            }

        cl_context ctx = static_cast<cl_context>(m_context);
        cl_command_queue q = static_cast<cl_command_queue>(m_queue);
        cl_kernel kern = static_cast<cl_kernel>(m_kernPearson);

        cl_mem bufX = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     flatX.size() * sizeof(float), flatX.data(), nullptr);
        cl_mem bufY = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     flatY.size() * sizeof(float), flatY.data(), nullptr);
        cl_mem bufR = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                     P * sizeof(float), nullptr, nullptr);

        clSetKernelArg(kern, 0, sizeof(cl_mem), &bufX);
        clSetKernelArg(kern, 1, sizeof(cl_mem), &bufY);
        clSetKernelArg(kern, 2, sizeof(cl_mem), &bufR);
        clSetKernelArg(kern, 3, sizeof(int), &P);
        clSetKernelArg(kern, 4, sizeof(int), &N);

        size_t global = P;
        clEnqueueNDRangeKernel(q, kern, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
        clFinish(q);

        std::vector<float> r(P);
        clEnqueueReadBuffer(q, bufR, CL_TRUE, 0, P * sizeof(float), r.data(), 0, nullptr, nullptr);
        clReleaseMemObject(bufX); clReleaseMemObject(bufY); clReleaseMemObject(bufR);

        for (int p = 0; p < P; ++p) result[p] = r[p];
        return result;
    }
#endif
    // CPU fallback
    for (int p = 0; p < P; ++p) {
        double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
        for (int i = 0; i < N; ++i) {
            double x = xs[p][i], y = ys[p][i];
            sx += x; sy += y; sxy += x * y; sx2 += x * x; sy2 += y * y;
        }
        double den = std::sqrt((N * sx2 - sx * sx) * (N * sy2 - sy * sy));
        result[p] = den > 0 ? (N * sxy - sx * sy) / den : 0;
    }
    return result;
}

// ── k-Means distances ──────────────────────────────────────

std::vector<std::vector<float>>
GpuCompute::kmeansDistances(const std::vector<std::vector<float>> &points,
                            const std::vector<std::vector<float>> &centroids) {
    int N = static_cast<int>(points.size());
    int K = static_cast<int>(centroids.size());
    std::vector<std::vector<float>> result(N, std::vector<float>(K, 0));
    if (N == 0 || K == 0) return result;
    int dim = static_cast<int>(points[0].size());

#ifdef HAS_OPENCL
    if (m_available && m_kernKmeans && N * K > 100) {
        std::vector<float> flatP(N * dim), flatC(K * dim);
        for (int i = 0; i < N; ++i)
            for (int d = 0; d < dim; ++d)
                flatP[i * dim + d] = points[i][d];
        for (int k = 0; k < K; ++k)
            for (int d = 0; d < dim; ++d)
                flatC[k * dim + d] = centroids[k][d];

        cl_context ctx = static_cast<cl_context>(m_context);
        cl_command_queue q = static_cast<cl_command_queue>(m_queue);
        cl_kernel kern = static_cast<cl_kernel>(m_kernKmeans);

        cl_mem bufP = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     flatP.size() * sizeof(float), flatP.data(), nullptr);
        cl_mem bufC = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     flatC.size() * sizeof(float), flatC.data(), nullptr);
        cl_mem bufD = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                     N * K * sizeof(float), nullptr, nullptr);

        clSetKernelArg(kern, 0, sizeof(cl_mem), &bufP);
        clSetKernelArg(kern, 1, sizeof(cl_mem), &bufC);
        clSetKernelArg(kern, 2, sizeof(cl_mem), &bufD);
        clSetKernelArg(kern, 3, sizeof(int), &N);
        clSetKernelArg(kern, 4, sizeof(int), &K);
        clSetKernelArg(kern, 5, sizeof(int), &dim);

        size_t global[2] = { (size_t)N, (size_t)K };
        clEnqueueNDRangeKernel(q, kern, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        clFinish(q);

        std::vector<float> dists(N * K);
        clEnqueueReadBuffer(q, bufD, CL_TRUE, 0, N * K * sizeof(float), dists.data(), 0, nullptr, nullptr);
        clReleaseMemObject(bufP); clReleaseMemObject(bufC); clReleaseMemObject(bufD);

        for (int i = 0; i < N; ++i)
            for (int k = 0; k < K; ++k)
                result[i][k] = dists[i * K + k];
        return result;
    }
#endif
    // CPU fallback
    for (int i = 0; i < N; ++i)
        for (int k = 0; k < K; ++k) {
            float d2 = 0;
            for (int d = 0; d < dim; ++d) {
                float diff = points[i][d] - centroids[k][d];
                d2 += diff * diff;
            }
            result[i][k] = d2;
        }
    return result;
}

// ── DBSCAN batch query ─────────────────────────────────────

std::vector<std::vector<int>>
GpuCompute::dbscanBatchQuery(const std::vector<std::vector<float>> &points,
                             const std::vector<std::vector<float>> &queries,
                             float eps2) {
    int N = static_cast<int>(points.size());
    int Q = static_cast<int>(queries.size());
    std::vector<std::vector<int>> result(Q);
    if (N == 0 || Q == 0) return result;
    int dim = static_cast<int>(points[0].size());

    // GPU: build distance matrix via k-means kernel (reuse point-distance pattern)
    // For now, CPU fallback with early termination
    // (Full GPU DBSCAN would need custom kernel for variable-length output)
    for (int q = 0; q < Q; ++q) {
        for (int i = 0; i < N; ++i) {
            float d2 = 0;
            for (int d = 0; d < dim; ++d) {
                float diff = points[i][d] - queries[q][d];
                d2 += diff * diff;
            }
            if (d2 <= eps2) result[q].push_back(i);
        }
    }
    return result;
}

// ── MLP forward pass ───────────────────────────────────────

std::vector<double>
GpuCompute::mlpForward(const std::vector<std::vector<double>> &X,
                       const std::vector<std::vector<double>> &w1,
                       const std::vector<double> &b1,
                       const std::vector<std::vector<double>> &w2,
                       const std::vector<double> &b2,
                       const std::vector<std::vector<double>> &w3,
                       double b3) {
    int batch = static_cast<int>(X.size());
    if (batch == 0) return {};
    int D = static_cast<int>(w1.size());
    int H1 = w1.empty() ? 0 : static_cast<int>(w1[0].size());
    int H2 = w2.empty() ? 0 : static_cast<int>(w2[0].size());

    std::vector<double> result(batch);
    // CPU implementation (GPU MLP requires larger batches to be worthwhile)
    for (int s = 0; s < batch; ++s) {
        std::vector<double> h1(H1, 0), h2(H2, 0);
        for (int j = 0; j < H1; ++j) {
            double z = b1[j];
            for (int i = 0; i < D; ++i) z += w1[i][j] * X[s][i];
            h1[j] = z > 0 ? z : 0;  // ReLU
        }
        for (int j = 0; j < H2; ++j) {
            double z = b2[j];
            for (int i = 0; i < H1; ++i) z += w2[i][j] * h1[i];
            h2[j] = z > 0 ? z : 0;
        }
        double out = b3;
        for (int i = 0; i < H2; ++i) out += w3[i][0] * h2[i];
        result[s] = 1.0 / (1.0 + std::exp(-out));  // sigmoid
    }
    return result;
}
