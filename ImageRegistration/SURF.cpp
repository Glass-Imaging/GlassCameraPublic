// Copyright (c) 2021-2022 Glass Imaging Inc.
// Author: Fabio Riccardi <fabio@glass-imaging.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <float.h>

#include <cmath>
#include <iostream>
#include <mutex>
#include <simd/simd.h>

#include "SURF.hpp"
#include "ThreadPool.hpp"
#include "feature2d.hpp"
#include "gls_mtl_image.hpp"
#include "gls_logging.h"

static const char* TAG = "DEMOSAIC";

#define USE_GPU_HESSIAN_DETECTOR true
// The integral pyramid seems to actually degrade performance
#define USE_INTEGRAL_PYRAMID false
#define USE_GPU_KEYPOINT_MATCH true

namespace gls {

static const int SURF_ORI_SEARCH_INC = 5;
static const float SURF_ORI_SIGMA = 2.5f;
static const float SURF_DESC_SIGMA = 3.3f;

// Wavelet size at first layer of first octave.
static const int SURF_HAAR_SIZE0 = 9;

// Wavelet size increment between layers. This should be an even number,
// such that the wavelet sizes in an octave are either all even or all odd.
// This ensures that when looking for the neighbours of a sample, the layers
// above and below are aligned correctly.
static const int SURF_HAAR_SIZE_INC = 6;

template <typename T>
void integral(const gls::image<float>& img, gls::image<T>* sum) {
    // Zero the first row and the first column of the sum
    for (int i = 0; i < sum->width; i++) {
        (*sum)[0][i] = 0;
    }
    for (int j = 1; j < sum->height; j++) {
        (*sum)[j][0] = 0;
    }

    for (int j = 1; j < sum->height; j++) {
        for (int i = 1; i < sum->width; i++) {
            // Use Signed Offset Pixel Representation to improve Integral Image precision
            // See: Hensley et al.: "Fast Summed-Area Table Generation and its Applications".
            (*sum)[j][i] = (img[j - 1][i - 1] - 0.5) + (*sum)[j][i - 1] + (*sum)[j - 1][i] - (*sum)[j - 1][i - 1];
        }
    }
}

struct SurfHF {
    std::array<gls::point, 4> p;
    float w;

    SurfHF() : p({gls::point{0, 0}, gls::point{0, 0}, gls::point{0, 0}, gls::point{0, 0}}), w(0) {}
};

inline std::ostream& operator<<(std::ostream& os, const SurfHF& hf) {
    os << "SurfHF - " << hf.p[0] << ", " << hf.p[1] << ", " << hf.p[2] << ", " << hf.p[3] << ", " << std::endl;
    return os;
}

float integralRectangle(float topRight, float topLeft, float bottomRight, float bottomLeft) {
    // Use Signed Offset Pixel Representation to improve Integral Image precision, see Integral Image code below
    return 0.5 + (topRight - topLeft) - (bottomRight - bottomLeft);
}

template <size_t N>
static inline float calcHaarPattern(const gls::image<float>& sum, const gls::point& p, const std::array<SurfHF, N>& f) {
    float d = 0;
    for (int k = 0; k < N; k++) {
        const auto& fk = f[k];

        float p0 = sum[p.y + fk.p[0].y][p.x + fk.p[0].x];
        float p1 = sum[p.y + fk.p[1].y][p.x + fk.p[1].x];
        float p2 = sum[p.y + fk.p[2].y][p.x + fk.p[2].x];
        float p3 = sum[p.y + fk.p[3].y][p.x + fk.p[3].x];

        d += fk.w * integralRectangle(p0, p1, p2, p3);
    }
    return d;
}

template <size_t N>
static void resizeHaarPattern(const int src[N][5], std::array<SurfHF, N>* dst, int oldSize, int newSize) {
    float ratio = (float)newSize / oldSize;
    for (int k = 0; k < N; k++) {
        int dx1 = (int)lrint(ratio * src[k][0]);
        int dy1 = (int)lrint(ratio * src[k][1]);
        int dx2 = (int)lrint(ratio * src[k][2]);
        int dy2 = (int)lrint(ratio * src[k][3]);
        (*dst)[k].p = {gls::point{dx1, dy1}, gls::point{dx1, dy2}, gls::point{dx2, dy1}, gls::point{dx2, dy2}};
        (*dst)[k].w = src[k][4] / ((float)(dx2 - dx1) * (dy2 - dy1));
    }
}

static void calcDetAndTrace(const gls::image<float>& sum, gls::image<float>* det, gls::image<float>* trace, int x,
                            int y, int sampleStep, const std::array<SurfHF, 3>& Dx, const std::array<SurfHF, 3>& Dy,
                            const std::array<SurfHF, 4>& Dxy) {
    const gls::point p = {x * sampleStep, y * sampleStep};

    float dx = calcHaarPattern(sum, p, Dx);
    float dy = calcHaarPattern(sum, p, Dy);
    float dxy = calcHaarPattern(sum, p, Dxy);

    (*det)[y][x] = dx * dy - 0.81f * dxy * dxy;
    (*trace)[y][x] = dx + dy;
}

/*
 * Maxima location interpolation as described in "Invariant Features from
 * Interest Point Groups" by Matthew Brown and David Lowe. This is performed by
 * fitting a 3D quadratic to a set of neighbouring samples.
 *
 * The gradient vector and Hessian matrix at the initial keypoint location are
 * approximated using central differences. The linear system Ax = b is then
 * solved, where A is the Hessian, b is the negative gradient, and x is the
 * offset of the interpolated maxima coordinates from the initial estimate.
 * This is equivalent to an iteration of Netwon's optimisation algorithm.
 *
 * N9 contains the samples in the 3x3x3 neighbourhood of the maxima
 * dx is the sampling step in x
 * dy is the sampling step in y
 * ds is the sampling step in size
 * point contains the keypoint coordinates and scale to be modified
 *
 * Return value is 1 if interpolation was successful, 0 on failure.
 */

inline float determinant(const gls::Matrix<3, 3>& A) {
    return A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
           A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
}

inline bool solve3x3(const gls::Matrix<3, 3>& A, const gls::Vector<3>& b, gls::Vector<3>* x) {
    float det = determinant(A);

    if (det != 0) {
        return gls::Vector<3>{determinant({b, A[1], A[2]}), determinant({A[0], b, A[2]}),
                              determinant({A[0], A[1], b})} /
               det;
        return true;
    }
    LOG_ERROR(TAG) << "solve3x3: Singular Matrix!" << std::endl;
    return false;
}

static bool interpolateKeypoint(const std::array<gls::image<float>, 3>& N9, int dx, int dy, int ds, KeyPoint* kpt) {
    // clang-format off
    gls::Vector<3> B = {
        -(N9[1][ 0][ 1] - N9[1][ 0][-1]) / 2, // Negative 1st deriv with respect to x
        -(N9[1][ 1][ 0] - N9[1][-1][ 0]) / 2, // Negative 1st deriv with respect to y
        -(N9[2][ 0][ 0] - N9[0][ 0][ 0]) / 2  // Negative 1st deriv with respect to s
    };
    gls::Matrix<3, 3> A = {
        {  N9[1][ 0][-1] - 2 * N9[1][ 0][ 0] + N9[1][ 0][ 1],                           // 2nd deriv x, x
          (N9[1][ 1][ 1] -     N9[1][ 1][-1] - N9[1][-1][ 1] + N9[1][-1][-1]) / 4,      // 2nd deriv x, y
          (N9[2][ 0][ 1] -     N9[2][ 0][-1] - N9[0][ 0][ 1] + N9[0][ 0][-1]) / 4 },    // 2nd deriv x, s
        { (N9[1][ 1][ 1] -     N9[1][ 1][-1] - N9[1][-1][ 1] + N9[1][-1][-1]) / 4,      // 2nd deriv x, y
           N9[1][-1][ 0] - 2 * N9[1][ 0][ 0] + N9[1][ 1][ 0],                           // 2nd deriv y, y
          (N9[2][ 1][ 0] -     N9[2][-1][ 0] - N9[0][ 1][ 0] + N9[0][-1][ 0]) / 4 },    // 2nd deriv y, s
        { (N9[2][ 0][ 1] -     N9[2][ 0][-1] - N9[0][ 0][ 1] + N9[0][ 0][-1]) / 4,      // 2nd deriv x, s
          (N9[2][ 1][ 0] -     N9[2][-1][ 0] - N9[0][ 1][ 0] + N9[0][-1][ 0]) / 4,      // 2nd deriv y, s
           N9[0][ 0][ 0] - 2 * N9[1][ 0][ 0] + N9[2][ 0][ 0] }                          // 2nd deriv s, s
    };
    // clang-format on

    gls::Vector<3> x;
    bool ok = solve3x3(A, B, &x);
    ok = ok && (x[0] != 0 || x[1] != 0 || x[2] != 0) && std::abs(x[0]) <= 1 && std::abs(x[1]) <= 1 &&
         std::abs(x[2]) <= 1;

    if (ok) {
        kpt->pt.x += x[0] * dx;
        kpt->pt.y += x[1] * dy;
        kpt->size = rint(kpt->size + x[2] * ds);
    }
    return ok;
}

struct gpuSurfHF {
    std::array<int, 8> p_dx[2];
    std::array<int, 8> p_dy[2];
    std::array<int, 8> p_dxy[4];

    gpuSurfHF(const std::array<SurfHF, 3>& Dx, const std::array<SurfHF, 3>& Dy, const std::array<SurfHF, 4>& Dxy) {
        /*
         NOTE: Removed repeating offsets from Dx and Dy, see note in SURF.metal
         */

        p_dx[0] = {Dx[0].p[0].x, Dx[0].p[0].y, Dx[0].p[1].x, Dx[0].p[1].y,
                   Dx[0].p[2].x, Dx[0].p[2].y, Dx[0].p[3].x, Dx[0].p[3].y};
        p_dx[1] = {Dx[1].p[2].x, Dx[1].p[2].y, Dx[1].p[3].x, Dx[1].p[3].y,
                   Dx[2].p[2].x, Dx[2].p[2].y, Dx[2].p[3].x, Dx[2].p[3].y};

        p_dy[0] = {Dy[0].p[0].x, Dy[0].p[0].y, Dy[0].p[1].x, Dy[0].p[1].y,
                   Dy[0].p[2].x, Dy[0].p[2].y, Dy[0].p[3].x, Dy[0].p[3].y};
        p_dy[1] = {Dy[1].p[1].x, Dy[1].p[1].y, Dy[1].p[3].x, Dy[1].p[3].y,
                   Dy[2].p[1].x, Dy[2].p[1].y, Dy[2].p[3].x, Dy[2].p[3].y};

        for (int i = 0; i < 4; i++) {
            p_dxy[i] = {Dxy[i].p[0].x, Dxy[i].p[0].y, Dxy[i].p[1].x, Dxy[i].p[1].y,
                        Dxy[i].p[2].x, Dxy[i].p[2].y, Dxy[i].p[3].x, Dxy[i].p[3].y};
        }
    }
};

struct DetAndTraceHaarPattern {
    static const int NX = 3, NY = 3, NXY = 4;

    std::array<SurfHF, NX> Dx;
    std::array<SurfHF, NY> Dy;
    std::array<SurfHF, NXY> Dxy;

    const gls::rectangle margin_crop;

    DetAndTraceHaarPattern(int sum_width, int sum_height, int size, int sampleStep)
        : margin_crop((size / 2) / sampleStep,  // Ignore pixels where some of the kernel is outside the image
                      (size / 2) / sampleStep,
                      1 + (sum_width - 1 - size) /
                              sampleStep,  // The integral image 'sum' is one pixel bigger than the source image
                      1 + (sum_height - 1 - size) / sampleStep) {
        // clang-format off
        const int dx_s[NX][5] = {
            {0, 2, 3, 7, 1},
            {3, 2, 6, 7, -2},
            {6, 2, 9, 7, 1}
        };
        const int dy_s[NY][5] = {
            {2, 0, 7, 3, 1},
            {2, 3, 7, 6, -2},
            {2, 6, 7, 9, 1}
        };
        const int dxy_s[NXY][5] = {
            {1, 1, 4, 4, 1},
            {5, 1, 8, 4, -1},
            {1, 5, 4, 8, -1},
            {5, 5, 8, 8, 1}
        };
        // clang-format on

        assert(size <= (sum_height - 1) || size > (sum_width - 1));

        resizeHaarPattern(dx_s, &Dx, 9, size);
        resizeHaarPattern(dy_s, &Dy, 9, size);
        resizeHaarPattern(dxy_s, &Dxy, 9, size);
    }

    // Rescale sampling points to the pyramid level
    void rescale(int scale) {
        for (auto& entry : Dx) {
            for (auto& pi : entry.p) {
                pi /= scale;
            }
        }
        for (auto& entry : Dy) {
            for (auto& pi : entry.p) {
                pi /= scale;
            }
        }
        for (auto& entry : Dxy) {
            for (auto& pi : entry.p) {
                pi /= scale;
            }
        }
    }

    // Rescale sampling points to the pyramid level
    void upscale(int scale) {
        for (auto& entry : Dx) {
            for (auto& pi : entry.p) {
                pi *= scale;
            }
        }
        for (auto& entry : Dy) {
            for (auto& pi : entry.p) {
                pi *= scale;
            }
        }
        for (auto& entry : Dxy) {
            for (auto& pi : entry.p) {
                pi *= scale;
            }
        }
    }
};

void calcLayerDetAndTrace(const gls::image<float>& sum, int size, int sampleStep, gls::image<float>* det,
                          gls::image<float>* trace) {
    DetAndTraceHaarPattern haarPattern(sum.width, sum.height, size, sampleStep);

    gls::image<float> detCpu = gls::image<float>(det, haarPattern.margin_crop);
    gls::image<float> traceCpu = gls::image<float>(*trace, haarPattern.margin_crop);

    for (int y = 0; y < haarPattern.margin_crop.height; y++) {
        for (int x = 0; x < haarPattern.margin_crop.width; x++) {
            calcDetAndTrace(sum, &detCpu, &traceCpu, x, y, sampleStep, haarPattern.Dx, haarPattern.Dy, haarPattern.Dxy);
        }
    }
}

void findMaximaInLayer(int width, int height, const std::array<gls::image<float>*, 3>& dets,
                       const gls::image<float>& trace, const std::array<int, 3>& sizes,
                       std::vector<KeyPoint>* keypoints, int octave, float hessianThreshold, int sampleStep,
                       std::mutex& keypointsMutex) {
    const int size = sizes[1];

    const int layer_height = height / sampleStep;
    const int layer_width = width / sampleStep;

    // Ignore pixels without a 3x3x3 neighbourhood in the layer above
    const int margin = (sizes[2] / 2) / sampleStep + 1;

    const gls::image<float>& det0 = *dets[0];
    const gls::image<float>& det1 = *dets[1];
    const gls::image<float>& det2 = *dets[2];

    int keyPointMaxima = 0;
    for (int y = margin; y < layer_height - margin; y++) {
        for (int x = margin; x < layer_width - margin; x++) {
            const float val0 = (*dets[1])[y][x];

            if (val0 > hessianThreshold) {
                /* Coordinates for the start of the wavelet in the sum image. There
                   is some integer division involved, so don't try to simplify this
                   (cancel out sampleStep) without checking the result is the same */
                int sum_y = sampleStep * (y - (size / 2) / sampleStep);
                int sum_x = sampleStep * (x - (size / 2) / sampleStep);

                /* The 3x3x3 neighbouring samples around the maxima.
                   The maxima is included at N9[1][0][0] */

                const std::array<gls::image<float>, 3> N9 = {
                    gls::image<float>(det0, {x, y, 1, 1}),
                    gls::image<float>(det1, {x, y, 1, 1}),
                    gls::image<float>(det2, {x, y, 1, 1}),
                };

                // clang-format off

                /* Non-maxima suppression. val0 is at N9[1][0][0] */
                if (val0 > N9[0][-1][-1] && val0 > N9[0][-1][0] && val0 > N9[0][-1][1] &&
                    val0 > N9[0][ 0][-1] && val0 > N9[0][ 0][0] && val0 > N9[0][ 0][1] &&
                    val0 > N9[0][ 1][-1] && val0 > N9[0][ 1][0] && val0 > N9[0][ 1][1] &&
                    val0 > N9[1][-1][-1] && val0 > N9[1][-1][0] && val0 > N9[1][-1][1] &&
                    val0 > N9[1][ 0][-1]                        && val0 > N9[1][ 0][1] &&
                    val0 > N9[1][ 1][-1] && val0 > N9[1][ 1][0] && val0 > N9[1][ 1][1] &&
                    val0 > N9[2][-1][-1] && val0 > N9[2][-1][0] && val0 > N9[2][-1][1] &&
                    val0 > N9[2][ 0][-1] && val0 > N9[2][ 0][0] && val0 > N9[2][ 0][1] &&
                    val0 > N9[2][ 1][-1] && val0 > N9[2][ 1][0] && val0 > N9[2][ 1][1])
                {
                    /* Calculate the wavelet center coordinates for the maxima */
                    float center_y = sum_y + (size - 1) * 0.5f;
                    float center_x = sum_x + (size - 1) * 0.5f;
                    KeyPoint kpt = {{center_x, center_y}, (float)sizes[1], -1, val0, octave, trace[y][x] > 0 };

                    /* Interpolate maxima location within the 3x3x3 neighbourhood  */
                    int ds = size - sizes[0];
                    int interp_ok = interpolateKeypoint(N9, sampleStep, sampleStep, ds, &kpt);

                    /* Sometimes the interpolation step gives a negative size etc. */
                    if (interp_ok) {
                        std::lock_guard<std::mutex> guard(keypointsMutex);
                        keypoints->push_back(kpt);
                        keyPointMaxima++;
                    }
                }
                // clang-format on
            }
        }
    }
    LOG_INFO(TAG) << "keyPointMaxima: " << keyPointMaxima << std::endl;
}

std::vector<float> getGaussianKernel(int n, float sigma) {
    const int SMALL_GAUSSIAN_SIZE = 7;
    // clang-format off
    static const float small_gaussian_tab[][SMALL_GAUSSIAN_SIZE] = {
        {1.f},
        {0.25f, 0.5f, 0.25f},
        {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f},
        {0.03125f, 0.109375f, 0.21875f, 0.28125f, 0.21875f, 0.109375f, 0.03125f}};
    // clang-format on

    const float* fixed_kernel =
        n % 2 == 1 && n <= SMALL_GAUSSIAN_SIZE && sigma <= 0 ? small_gaussian_tab[n >> 1] : nullptr;

    float sigmaX = sigma > 0 ? sigma : ((n - 1) * 0.5 - 1) * 0.3 + 0.8;
    float scale2X = -0.5 / (sigmaX * sigmaX);
    float sum = 0;

    std::vector<float> kernel(n);
    for (int i = 0; i < n; i++) {
        float x = i - (n - 1) * 0.5;
        float t = fixed_kernel ? (float)fixed_kernel[i] : std::exp(scale2X * x * x);
        kernel[i] = t;
        sum += kernel[i];
    }

    sum = 1. / sum;
    for (auto& v : kernel) {
        v *= sum;
    }

    return kernel;
}

void resizeVV(const gls::image<float>& src, gls::image<float>* dst, int interpolation) {
    // Note that src and dst represent square matrices

    float dsize = (float)src.height / dst->height;
    if (dst->height && dst->width) {
        for (int i = 0; i < dst->height; i++) {
            for (int j = 0; j < dst->width; j++) {
                int _i = (int)i * dsize;       // integer part
                float _idec = i * dsize - _i;  // fractional part
                int _j = (int)j * dsize;
                float _jdec = j * dsize - _j;
                if (_j >= 0 && _j < (src.height - 1) && _i >= 0 && _i < (src.height - 1)) {
                    // Bilinear interpolation
                    (*dst)[i][j] = (1 - _idec) * (1 - _jdec) * src[_i][_j] + _idec * (1 - _jdec) * src[_i + 1][_j] +
                                   _jdec * (1 - _idec) * src[_j][_j + 1] + _idec * _jdec * src[_i + 1][_j + 1];
                }
            }
        }
    }
}

struct SURFInvoker {
    enum { ORI_RADIUS = 6, ORI_WIN = 60, PATCH_SZ = 20 };

    // Simple bound for number of grid points in circle of radius ORI_RADIUS
    const int nOriSampleBound = (2 * ORI_RADIUS + 1) * (2 * ORI_RADIUS + 1);

    // Parameters
    const gls::image<float>& img;
    const gls::image<float>& sum;
    std::vector<KeyPoint>* keypoints;
    gls::image<float>* descriptors;

    // Pre-calculated values
    int nOriSamples;
    std::vector<Point2f> apt;
    std::vector<float> aptw;
    std::vector<float> DW;

    SURFInvoker(const gls::image<float>& _img, const gls::image<float>& _sum, std::vector<KeyPoint>* _keypoints,
                gls::image<float>* _descriptors)
        : img(_img), sum(_sum), keypoints(_keypoints), descriptors(_descriptors) {
        enum { ORI_RADIUS = 6, ORI_WIN = 60, PATCH_SZ = 20 };

        // Simple bound for number of grid points in circle of radius ORI_RADIUS
        const int nOriSampleBound = (2 * ORI_RADIUS + 1) * (2 * ORI_RADIUS + 1);

        // Allocate arrays
        apt.resize(nOriSampleBound);
        aptw.resize(nOriSampleBound);
        DW.resize(PATCH_SZ * PATCH_SZ);

        /* Coordinates and weights of samples used to calculate orientation */
        const auto G_ori = getGaussianKernel(2 * ORI_RADIUS + 1, SURF_ORI_SIGMA);
        nOriSamples = 0;
        for (int i = -ORI_RADIUS; i <= ORI_RADIUS; i++) {
            for (int j = -ORI_RADIUS; j <= ORI_RADIUS; j++) {
                if (i * i + j * j <= ORI_RADIUS * ORI_RADIUS) {
                    apt[nOriSamples] = Point2f(i, j);
                    aptw[nOriSamples++] = G_ori[i + ORI_RADIUS] * G_ori[j + ORI_RADIUS];
                }
            }
        }
        assert(nOriSamples <= nOriSampleBound);

        /* Gaussian used to weight descriptor samples */
        const auto G_desc = getGaussianKernel(PATCH_SZ, SURF_DESC_SIGMA);
        for (int i = 0; i < PATCH_SZ; i++) {
            for (int j = 0; j < PATCH_SZ; j++) DW[i * PATCH_SZ + j] = G_desc[i] * G_desc[j];
        }
    }

    void computeRange(int k1, int k2) {
        /* X and Y gradient wavelet data */
        const int NX = 2, NY = 2;
        const int dx_s[NX][5] = {{0, 0, 2, 4, -1}, {2, 0, 4, 4, 1}};
        const int dy_s[NY][5] = {{0, 0, 4, 2, 1}, {0, 2, 4, 4, -1}};

        float X[nOriSampleBound], Y[nOriSampleBound], angle[nOriSampleBound];
        gls::image<float> PATCH(PATCH_SZ + 1, PATCH_SZ + 1);
        float DX[PATCH_SZ][PATCH_SZ], DY[PATCH_SZ][PATCH_SZ];

        // TODO: should we also add the extended (dsize = 128) case?
        const int dsize = 64;

        float maxSize = 0;
        for (int k = k1; k < k2; k++) {
            maxSize = std::max(maxSize, (*keypoints)[k].size);
        }

        for (int k = k1; k < k2; k++) {
            std::array<SurfHF, NX> dx_t;
            std::array<SurfHF, NY> dy_t;
            KeyPoint& kp = (*keypoints)[k];
            float size = kp.size;
            Point2f center = kp.pt;
            /* The sampling intervals and wavelet sized for selecting an orientation
             and building the keypoint descriptor are defined relative to 's' */
            float s = size * 1.2f / 9.0f;
            /* To find the dominant orientation, the gradients in x and y are
             sampled in a circle of radius 6s using wavelets of size 4s.
             We ensure the gradient wavelet size is even to ensure the
             wavelet pattern is balanced and symmetric around its center */
            int grad_wav_size = 2 * (int)lrint(2 * s);
            if (sum.height < grad_wav_size || sum.width < grad_wav_size) {
                /* when grad_wav_size is too big,
                 * the sampling of gradient will be meaningless
                 * mark keypoint for deletion. */
                kp.size = -1;
                continue;
            }

            float descriptor_dir = 360.f - 90.f;
            resizeHaarPattern(dx_s, &dx_t, 4, grad_wav_size);
            resizeHaarPattern(dy_s, &dy_t, 4, grad_wav_size);
            int nangle = 0;
            for (int kk = 0; kk < nOriSamples; kk++) {
                // TODO: if we use round instead of lrint the result is slightly different

                int x = (int)lrint(center.x + apt[kk].x * s - (float)(grad_wav_size - 1) / 2);
                int y = (int)lrint(center.y + apt[kk].y * s - (float)(grad_wav_size - 1) / 2);
                if (y < 0 || y >= sum.height - grad_wav_size || x < 0 || x >= sum.width - grad_wav_size) continue;
                float vx = calcHaarPattern(sum, {x, y}, dx_t);
                float vy = calcHaarPattern(sum, {x, y}, dy_t);
                X[nangle] = vx * aptw[kk];
                Y[nangle] = vy * aptw[kk];
                nangle++;
            }
            if (nangle == 0) {
                // No gradient could be sampled because the keypoint is too
                // near too one or more of the sides of the image. As we
                // therefore cannot find a dominant direction, we skip this
                // keypoint and mark it for later deletion from the sequence.
                kp.size = -1;
                continue;
            }

            // phase( Mat(1, nangle, CV_32F, X), Mat(1, nangle, CV_32F, Y), Mat(1, nangle, CV_32F, angle), true );
            for (int i = 0; i < nangle; i++) {
                float temp = atan2(Y[i], X[i]) * (180 / M_PI);
                if (temp < 0)
                    angle[i] = temp + 360;
                else
                    angle[i] = temp;
            }

            float bestx = 0, besty = 0, descriptor_mod = 0;
            for (float i = 0; i < 360; i += SURF_ORI_SEARCH_INC) {
                float sumx = 0, sumy = 0, temp_mod;
                for (int j = 0; j < nangle; j++) {
                    float d = std::abs(lrint(angle[j]) - i);
                    if (d < ORI_WIN / 2 || d > 360 - ORI_WIN / 2) {
                        sumx += X[j];
                        sumy += Y[j];
                    }
                }
                temp_mod = sumx * sumx + sumy * sumy;
                if (temp_mod > descriptor_mod) {
                    descriptor_mod = temp_mod;
                    bestx = sumx;
                    besty = sumy;
                }
            }
            descriptor_dir = atan2(-besty, bestx);

            kp.angle = descriptor_dir;

            if (!descriptors) continue;

            /* Extract a window of pixels around the keypoint of size 20s */
            int win_size = (int)((PATCH_SZ + 1) * s);
            gls::image<float> mwin(win_size, win_size);

            // !upright
            descriptor_dir *= (float)(M_PI / 180);
            float sin_dir = -std::sin(descriptor_dir);
            float cos_dir = std::cos(descriptor_dir);

            float win_offset = -(float)(win_size - 1) / 2;
            float start_x = center.x + win_offset * cos_dir + win_offset * sin_dir;
            float start_y = center.y - win_offset * sin_dir + win_offset * cos_dir;

            int ncols1 = img.width - 1, nrows1 = img.height - 1;
            for (int i = 0; i < win_size; i++, start_x += sin_dir, start_y += cos_dir) {
                float pixel_x = start_x;
                float pixel_y = start_y;
                for (int j = 0; j < win_size; j++, pixel_x += cos_dir, pixel_y -= sin_dir) {
                    int ix = std::floor(pixel_x);
                    int iy = std::floor(pixel_y);

                    if ((unsigned)ix < (unsigned)ncols1 && (unsigned)iy < (unsigned)nrows1) {
                        float a = pixel_x - ix;
                        float b = pixel_y - iy;

                        mwin[i][j] = std::round((img[iy][ix] * (1 - a) + img[iy][ix + 1] * a) * (1 - b) +
                                                (img[iy + 1][ix] * (1 - a) + img[iy + 1][ix + 1] * a) * b);
                    } else {
                        int x = std::clamp((int)std::round(pixel_x), 0, ncols1);
                        int y = std::clamp((int)std::round(pixel_y), 0, nrows1);
                        mwin[i][j] = img[y][x];
                    }
                }
            }

            // Scale the window to size PATCH_SZ so each pixel's size is s. This
            // makes calculating the gradients with wavelets of size 2s easy
            resizeVV(mwin, &PATCH, 0);

            // Calculate gradients in x and y with wavelets of size 2s
            for (int i = 0; i < PATCH_SZ; i++)
                for (int j = 0; j < PATCH_SZ; j++) {
                    float dw = DW[i * PATCH_SZ + j];
                    float vx = (PATCH[i][j + 1] - PATCH[i][j] + PATCH[i + 1][j + 1] - PATCH[i + 1][j]) * dw;
                    float vy = (PATCH[i + 1][j] - PATCH[i][j] + PATCH[i + 1][j + 1] - PATCH[i][j + 1]) * dw;
                    DX[i][j] = vx;
                    DY[i][j] = vy;
                }

            // Construct the descriptor
            for (int kk = 0; kk < dsize; kk++) {
                (*descriptors)[k][kk] = 0;
            }
            float square_mag = 0;

            // 64-bin descriptor
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    int index = 16 * i + 4 * j;

                    for (int y = i * 5; y < i * 5 + 5; y++) {
                        for (int x = j * 5; x < j * 5 + 5; x++) {
                            float tx = DX[y][x], ty = DY[y][x];
                            (*descriptors)[k][index + 0] += tx;
                            (*descriptors)[k][index + 1] += ty;
                            (*descriptors)[k][index + 2] += (float)fabs(tx);
                            (*descriptors)[k][index + 3] += (float)fabs(ty);
                        }
                    }
                    for (int kk = 0; kk < 4; kk++) {
                        float v = (*descriptors)[k][index + kk];
                        square_mag += v * v;
                    }
                }
            }

            // unit vector is essential for contrast invariance
            float scale = (float)(1. / (std::sqrt(square_mag) + FLT_EPSILON));
            for (int kk = 0; kk < dsize; kk++) {
                (*descriptors)[k][kk] *= scale;
            }
        }
    }

    void run() {
        const int K = (int)keypoints->size();

        if (K > 32) {
            const int threads = 8;
            ThreadPool threadPool(threads);

            const int ranges = (int)std::ceil((float)K / threads);
            for (int rr = 0; rr < ranges; rr++) {
                int k1 = ranges * rr;
                int k2 = std::min(ranges * (rr + 1), K);

                threadPool.enqueue([this, k1, k2]() { computeRange(k1, k2); });
            }
        } else {
            computeRange(0, K);
        }
    }
};

void descriptor(const gls::image<float>& srcImg, const gls::image<float>& integralSum, std::vector<KeyPoint>* keypoints,
                gls::image<float>* descriptors) {
    int N = (int)keypoints->size();
    if (N > 0) {
        SURFInvoker(srcImg, integralSum, keypoints, descriptors).run();
    }
}

template <typename T, int N = 4>
std::array<typename gls::mtl_image_2d<T>::unique_ptr, N> sumImageStack(MetalContext* context, int width, int height) {
    std::array<typename gls::mtl_image_2d<T>::unique_ptr, N> result;
    for (int i = 0; i < N; i++) {
        int step = 1 << i;
        result[i] =
            std::make_unique<gls::mtl_image_2d<T>>(context->device(), 1 + (width - 1) / step, 1 + (height - 1) / step);
    }
    return result;
}

void SURFBuild(const gls::image<float>& sum, const std::vector<int>& sizes,
               const std::vector<int>& sampleSteps, const std::vector<gls::image<float>::unique_ptr>& dets,
               const std::vector<gls::image<float>::unique_ptr>& traces, int nOctaves, int nOctaveLayers) {
    int N = (int)sizes.size();
    LOG_INFO(TAG) << "enqueueing " << N << " calcLayerDetAndTrace" << std::endl;

    ThreadPool threadPool(8);

    const int layers = nOctaveLayers + 2;

    assert(nOctaves * layers == N);

    for (int octave = 0; octave < nOctaves; octave++) {
        for (int layer = 0; layer < layers; layer++) {
            const int i = octave * layers + layer;
            /*
                 RANSAC interior point ratio - number of loops: 121 150 39
                  Transformation matrix parameter:
                 0.989685 0.027618 84.000000
                -0.030334 0.993164 119.187500
                -0.000002 -0.000002 1
             */
            threadPool.enqueue([&sum, &dets, &traces, &sizes, &sampleSteps, i]() {
                calcLayerDetAndTrace(sum, sizes[i], sampleSteps[i], dets[i].get(), traces[i].get());
            });
        }
    }
}

void SURFFind(const gls::image<float>& sum, const std::vector<gls::image<float>::unique_ptr>& dets,
              const std::vector<gls::image<float>::unique_ptr>& traces, const std::vector<int>& sizes,
              const std::vector<int>& sampleSteps, const std::vector<int>& middleIndices,
              std::vector<KeyPoint>* keypoints, int nOctaveLayers, float hessianThreshold) {
    std::mutex keypointsMutex;
    ThreadPool threadPool(8);

    int M = (int)middleIndices.size();
    LOG_INFO(TAG) << "enqueueing " << M << " findMaximaInLayer" << std::endl;
    for (int i = 0; i < M; i++) {
        const int layer = middleIndices[i];
        const int octave = i / nOctaveLayers;

        threadPool.enqueue([&sum, &dets, &traces, &sizes, &sampleSteps, &keypoints, &keypointsMutex, layer, octave,
                            hessianThreshold]() {
            auto dets0 = dets[layer - 1].get();
            auto dets1 = dets[layer].get();
            auto dets2 = dets[layer + 1].get();

            auto traceImage = traces[layer].get();

            findMaximaInLayer(sum.width - 1, sum.height - 1, {dets0, dets1, dets2}, *traceImage,
                              {sizes[layer - 1], sizes[layer], sizes[layer + 1]}, keypoints, octave, hessianThreshold,
                              sampleSteps[layer], keypointsMutex);
        });
    }
}

struct integralImageKernel {
    const gls::size imageSize;
    const gls::size tmpSize;
    static const int tileSize = 8;

    NS::SharedPtr<MTL::Buffer> _integralTmpBuffer;
    gls::mtl_image_2d<float>::unique_ptr _integralInputImage = nullptr;

    Kernel<
        MTL::Texture*,  // sourceImage
        MTL::Buffer*,   // buf_ptr
        int             // buf_width
    > integral_sum_cols;

    Kernel<
        MTL::Buffer*,   // buf_ptr
        int,            // buf_width
        MTL::Texture*,  // sum0
        MTL::Texture*,  // sum1
        MTL::Texture*,  // sum2
        MTL::Texture*   // sum3
    > integral_sum_rows;

    integralImageKernel(MetalContext* context, gls::size _imageSize) :
    imageSize(_imageSize),
    tmpSize(((_imageSize.height + tileSize - 1) / tileSize) * tileSize,
                      ((_imageSize.width + tileSize - 1) / tileSize) * tileSize),
    integral_sum_cols(context, "integral_sum_cols_image"),
    integral_sum_rows(context, "integral_sum_rows_image")
    {
        _integralTmpBuffer = NS::TransferPtr(context->device()->newBuffer(sizeof(float) * tmpSize.width * tmpSize.height, MTL::ResourceStorageModeShared));
        _integralInputImage = std::make_unique<gls::mtl_image_2d<float>>(context->device(), _imageSize.width, _imageSize.height);
    }

    void operator() (MetalContext* context, const gls::image<float>& inputImage,
                     const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum) const {
        assert(inputImage.size() == imageSize);

        _integralInputImage->copyPixelsFrom(inputImage);

        integral_sum_cols(context, /*gridSize=*/ MTL::Size(imageSize.width, 1, 1), /*threadGroupSize=*/ MTL::Size(tileSize, 1, 1),
                          _integralInputImage->texture(), _integralTmpBuffer.get(), tmpSize.width);

        integral_sum_rows(context, /*gridSize=*/ MTL::Size(imageSize.height, 1, 1), /*threadGroupSize=*/ MTL::Size(tileSize, 1, 1),
                          _integralTmpBuffer.get(), tmpSize.width, sum[0]->texture(), sum[1]->texture(), sum[2]->texture(), sum[3]->texture());

        // TODO: verify that this is a good idea
        context->waitForCompletion();
    }
};

struct calcDetAndTraceKernel {
    NS::SharedPtr<MTL::Buffer> _surfHFDataBuffer;

    Kernel<
        MTL::Texture*,  // sumImage
        MTL::Texture*,  // detImage
        MTL::Texture*,  // traceImage
        int,            // sampleStep
        simd::float2,   // w
        simd::int2,     // margin
        MTL::Buffer*    // surfHFData
    > calcDetAndTrace;

    calcDetAndTraceKernel(MetalContext* context) :
    calcDetAndTrace(context, "calcDetAndTrace")
    {
        _surfHFDataBuffer = NS::TransferPtr(context->device()->newBuffer(sizeof(gpuSurfHF), MTL::ResourceStorageModeShared));
    }

    void operator() (MetalContext* context, const gls::mtl_image_2d<float>& sumImage,
                     gls::mtl_image_2d<float>* detImage, gls::mtl_image_2d<float>* traceImage,
                     const int sampleStep, const DetAndTraceHaarPattern& haarPattern) const {
        const gpuSurfHF surfHFData(haarPattern.Dx, haarPattern.Dy, haarPattern.Dxy);
        std::memmove(_surfHFDataBuffer->contents(), &surfHFData, sizeof(gpuSurfHF));

        const auto& margin_crop = haarPattern.margin_crop;

        calcDetAndTrace(context, /*gridSize=*/ MTL::Size(margin_crop.width, margin_crop.height, 1),
                        sumImage.texture(), detImage->texture(), traceImage->texture(), sampleStep,
                        simd::float2 {haarPattern.Dx[0].w, haarPattern.Dxy[0].w},
                        simd::int2 {margin_crop.x, margin_crop.y},
                        _surfHFDataBuffer.get());

        // TODO: verify that this is a good idea
        context->waitForCompletion();
    }
};

/*
 * Find the maxima in the determinant of the Hessian in a layer of the
 * scale-space pyramid
 */

typedef struct KeyPointMaxima {
    static constexpr int MaxCount = 64000;
    int count;
    KeyPoint keyPoints[MaxCount];
} KeyPointMaxima;

struct findMaximaInLayerKernel {
    const gls::size imageSize;
    NS::SharedPtr<MTL::Buffer> _keyPointsBuffer;

    Kernel<
        MTL::Texture*,  // detImage0
        MTL::Texture*,  // detImage1
        MTL::Texture*,  // detImage2
        MTL::Texture*,  // traceImage
        simd::int3,     // sizes
        MTL::Buffer*,   // keypoints
        int,            // margin
        int,            // octave
        float,          // hessianThreshold
        int             // sampleStep
    > findMaximaInLayer;

    findMaximaInLayerKernel(MetalContext* context, gls::size _imageSize) :
    imageSize(_imageSize),
    findMaximaInLayer(context, "findMaximaInLayer") {
        _keyPointsBuffer = NS::TransferPtr(context->device()->newBuffer(sizeof(KeyPointMaxima), MTL::ResourceStorageModeShared));
    }

    void operator() (MetalContext* context, const std::array<const gls::mtl_image_2d<float>*, 3>& dets,
                     const gls::mtl_image_2d<float>& traceImage, const std::array<int, 3>& sizes,
                     int octave, float hessianThreshold, int sampleStep) const {
        const int layer_height = imageSize.height / sampleStep;
        const int layer_width = imageSize.width / sampleStep;

        // Ignore pixels without a 3x3x3 neighbourhood in the layer above
        const int margin = (sizes[2] / 2) / sampleStep + 1;

        findMaximaInLayer(context, /*gridSize=*/ MTL::Size(layer_width - 2 * margin, layer_height - 2 * margin, 1),
                          dets[0]->texture(), dets[1]->texture(), dets[2]->texture(), traceImage.texture(),
                          simd::int3 {sizes[0], sizes[1], sizes[2]}, _keyPointsBuffer.get(), margin, octave, hessianThreshold, sampleStep);

        // TODO: verify that this is a good idea
        context->waitForCompletion();
    }
};

struct refineMatch {
    inline bool operator()(const DMatch& mp1, const DMatch& mp2) const {
        if (mp1.distance < mp2.distance) return true;
        if (mp1.distance > mp2.distance) return false;
        // if (mp1.queryIdx < mp2.queryIdx) return true;
        // if (mp1.queryIdx > mp2.queryIdx) return false;
        /*if (mp1.octave > mp2.octave) return true;
        if (mp1.octave < mp2.octave) return false;
        if (mp1.pt.y < mp2.pt.y) return false;
        if (mp1.pt.y > mp2.pt.y) return true;*/
        return mp1.queryIdx < mp2.queryIdx;
    }
};

struct matchKeyPointsKernel {
    Kernel<
        MTL::Buffer*,   // descriptor1
        MTL::Buffer*,   // descriptor2
        int,            // descriptor2_height
        MTL::Buffer*    // matchedPoints
    > matchKeyPoints;

    static constexpr int match_block_size = 24;

    matchKeyPointsKernel(MetalContext* context) : matchKeyPoints(context, "matchKeyPoints") { }

    std::vector<DMatch> operator() (MetalContext* context, const gls::image<float>& descriptor1, const gls::image<float>& descriptor2) const {
        assert(descriptor1.stride == 64 && descriptor2.stride == 64);

        std::cout << "Matching descriptors " << descriptor1.height << ", " << descriptor2.height << std::endl;

        auto descriptor1Buffer = gls::Buffer<float>(context->device(), descriptor1.pixels());
        auto descriptor2Buffer = gls::Buffer<float>(context->device(), descriptor2.pixels());
        auto matchesBuffer = gls::Buffer<DMatch>(context->device(), descriptor1.height);

        matchKeyPoints(context,
                       /*gridSize=*/ MTL::Size(descriptor1.height, match_block_size, 1),
                       /*threadGroupSize=*/ MTL::Size(1, match_block_size, 1),
                       descriptor1Buffer.buffer(),
                       descriptor2Buffer.buffer(),
                       descriptor2.height, matchesBuffer.buffer());

        // TODO: verify that this is a good idea
        context->waitForCompletion();

        // Collect results
        const std::span<DMatch> newElements(matchesBuffer.data(), descriptor1.height);

        // Build result vector
        std::vector<DMatch> matchedPoints(begin(newElements), end(newElements));

        // cl::enqueueUnmapMemObject(matchesBuffer, (void*)matches);

        std::sort(matchedPoints.begin(), matchedPoints.end(), refineMatch());  // feature point sorting

        return matchedPoints;
    }
};

static inline float L2Norm(const std::array<float, 64>& p1, const std::array<float, 64>& p2) {
    float sum = 0;
    for (uint i = 0; i < p1.size(); i++) {
        float diff = p1[i] - p2[i];
        sum += diff * diff;
    }
    return sqrtf(sum);
}

// Brute force CPU kerypoint matching
void matchKeyPoints(const gls::image<float>& descriptor1, const gls::image<float>& descriptor2,
                    std::vector<DMatch>* matchedPoints) {
    assert(descriptor1.width == 64 && descriptor2.width == 64);

    for (int i = 0; i < descriptor1.height; i++) {
        const std::array<float, 64>& p1 = *(const std::array<float, 64>*) descriptor1[i];
        float distance_min = std::numeric_limits<float>::max();
        int j_min = 0, i_min = 0;

        for (int j = 0; j < descriptor2.height; j++) {
            const std::array<float, 64>& p2 = *(const std::array<float, 64>*) descriptor2[j];
            // calculate distance
            float distance_t = L2Norm(p1, p2);
            if (distance_t < distance_min) {
                distance_min = distance_t;
                i_min = i;
                j_min = j;
            }
        }

        matchedPoints->push_back(DMatch(i_min, j_min, distance_min));
    }

    std::sort(matchedPoints->begin(), matchedPoints->end(), refineMatch());  // feature point sorting
}

class SURFGPU : public SURF {
   private:
    MetalContext* _gpuContext;

    const int _width;
    const int _height;
    const int _max_features;
    const int _nOctaves;
    const int _nOctaveLayers;
    const float _hessianThreshold;

    integralImageKernel _integralImage;
    calcDetAndTraceKernel _calcDetAndTrace;
    findMaximaInLayerKernel _findMaximaInLayer;
    matchKeyPointsKernel _matchKeyPoints;

    std::vector<gls::mtl_image_2d<float>::unique_ptr> _dets;
    std::vector<gls::mtl_image_2d<float>::unique_ptr> _traces;

    /* Sampling step along image x and y axes at first octave. This is doubled
    for each additional octave. WARNING: Increasing this improves speed,
    however keypoint extraction becomes unreliable. */
    static const int SAMPLE_STEP0 = 1;

    void calcDetAndTrace(const gls::mtl_image_2d<float>& sumImage, gls::mtl_image_2d<float>* detImage,
                         gls::mtl_image_2d<float>* traceImage, const int sampleStep,
                         const DetAndTraceHaarPattern& haarPattern) const {
        _calcDetAndTrace(_gpuContext, sumImage, detImage, traceImage, sampleStep, haarPattern);
    }

    void findMaximaInLayer(const std::array<const gls::mtl_image_2d<float>*, 3>& dets,
                           const gls::mtl_image_2d<float>& traceImage, const std::array<int, 3>& sizes, int octave,
                           float hessianThreshold, int sampleStep) const;

    void Build(const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum, const std::vector<int>& sizes,
               const std::vector<int>& sampleSteps, const std::vector<gls::mtl_image_2d<float>::unique_ptr>& dets,
               const std::vector<gls::mtl_image_2d<float>::unique_ptr>& traces) const;

    void Find(const std::vector<gls::mtl_image_2d<float>::unique_ptr>& dets,
              const std::vector<gls::mtl_image_2d<float>::unique_ptr>& traces, const std::vector<int>& sizes,
              const std::vector<int>& sampleSteps, const std::vector<int>& middleIndices,
              std::vector<KeyPoint>* keypoints, int nOctaveLayers, float hessianThreshold) const;

    void fastHessianDetector(const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum,
                             std::vector<KeyPoint>* keypoints, int nOctaves, int nOctaveLayers, float hessianThreshold) const;

   public:
    SURFGPU(MetalContext* glsContext, int width, int height, int max_features = -1, int nOctaves = 4,
                int nOctaveLayers = 2, float hessianThreshold = 0.02);

    void integral(const gls::image<float>& inputImage, const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum) const override  {
        _integralImage(_gpuContext, inputImage, sum);
    }

    void detect(const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& integralSum,
                std::vector<KeyPoint>* keypoints) const override {
        fastHessianDetector(integralSum, keypoints, _nOctaves, _nOctaveLayers, _hessianThreshold);
    }

    void detectAndCompute(const gls::image<float>& img, std::vector<KeyPoint>* keypoints,
                          gls::image<float>::unique_ptr* _descriptors) const override {
        detectAndCompute(img, keypoints, _descriptors, /*sections=*/ {1, 1});
    }

    void detectAndCompute(const gls::image<float>& img, std::vector<KeyPoint>* keypoints,
                          gls::image<float>::unique_ptr* _descriptors, gls::size sections) const;

    std::vector<DMatch> matchKeyPoints(const gls::image<float>& descriptor1,
                                       const gls::image<float>& descriptor2) const override {
#if USE_GPU_KEYPOINT_MATCH
        return _matchKeyPoints(_gpuContext, descriptor1, descriptor2);
#else
        std::vector<DMatch> matchedPoints;
        gls::matchKeyPoints(descriptor1, descriptor2, &matchedPoints);
        return matchedPoints;
#endif
    }
};

std::unique_ptr<SURF> SURF::makeInstance(MetalContext* glsContext, int width, int height, int max_features,
                                         int nOctaves, int nOctaveLayers, float hessianThreshold) {
    return std::make_unique<SURFGPU>(glsContext, width, height, max_features, nOctaves, nOctaveLayers,
                                         hessianThreshold);
}

SURFGPU::SURFGPU(MetalContext* glsContext, int width, int height, int max_features, int nOctaves,
                         int nOctaveLayers, float hessianThreshold)
    : _gpuContext(glsContext),
      _width(width),
      _height(height),
      _max_features(max_features),
      _nOctaves(nOctaves),
      _nOctaveLayers(nOctaveLayers),
      _hessianThreshold(hessianThreshold),
      _integralImage(glsContext, {width, height}),
      _calcDetAndTrace(glsContext),
      _findMaximaInLayer(glsContext, {width, height}),
      _matchKeyPoints(glsContext)
{
    int nTotalLayers = (nOctaveLayers + 2) * nOctaves;

    if (_dets.size() != nTotalLayers) {
        LOG_INFO(TAG) << "resizing dets and traces vectors to " << nTotalLayers << std::endl;
        _dets.resize(nTotalLayers);
        _traces.resize(nTotalLayers);
    }

    // Allocate space for each layer
    int index = 0, step = SAMPLE_STEP0;

    for (int octave = 0; octave < nOctaves; octave++) {
        for (int layer = 0; layer < nOctaveLayers + 2; layer++) {
            /* The integral image sum is one pixel bigger than the source image*/
            if (_dets[index] == nullptr) {
                _dets[index] = std::make_unique<gls::mtl_image_2d<float>>(_gpuContext->device(), width / step,
                                                                                height / step);
                _traces[index] = std::make_unique<gls::mtl_image_2d<float>>(_gpuContext->device(),
                                                                                  width / step, height / step);
            }
            index++;
        }
        step *= 2;
    }
}

void SURFGPU::Build(const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum, const std::vector<int>& sizes,
                    const std::vector<int>& sampleSteps,
                    const std::vector<gls::mtl_image_2d<float>::unique_ptr>& dets,
                    const std::vector<gls::mtl_image_2d<float>::unique_ptr>& traces) const {
    int N = (int)sizes.size();
    LOG_INFO(TAG) << "enqueueing " << N << " calcLayerDetAndTrace" << std::endl;

    const int layers = _nOctaveLayers + 2;

    assert(_nOctaves * layers == N);

    for (int octave = 0; octave < _nOctaves; octave++) {
        for (int layer = 0; layer < layers; layer++) {
            const int i = octave * layers + layer;
            DetAndTraceHaarPattern haarPattern(sum[0]->width, sum[0]->height, sizes[i], sampleSteps[i]);

//            LOG_INFO(TAG) << "DetAndTraceHaarPattern: " << sum[0]->width << ", " << sum[0]->height << ", "
//            << sizes[i] << ", " << sampleSteps[i] << std::endl;

#if USE_INTEGRAL_PYRAMID
            // Rescale sampling points to the pyramid level
            haarPattern.rescale(sampleSteps[i]);

            const int idx = sampleSteps[i] == 8 ? 3 : sampleSteps[i] == 4 ? 2 : sampleSteps[i] == 2 ? 1 : 0;
            calcDetAndTrace(*sum[idx], dets[i].get(), traces[i].get(), 1, haarPattern);
#else
//            // Emulate the integral pyramid scaling roundoff
//            haarPattern.rescale(sampleSteps[i]);
//            haarPattern.upscale(sampleSteps[i]);

            calcDetAndTrace(*sum[0], dets[i].get(), traces[i].get(), sampleSteps[i], haarPattern);
#endif
        }
    }
}

void SURFGPU::findMaximaInLayer(const std::array<const gls::mtl_image_2d<float>*, 3>& dets,
                                const gls::mtl_image_2d<float>& traceImage, const std::array<int, 3>& sizes,
                                int octave, float hessianThreshold, int sampleStep) const {
    _findMaximaInLayer(_gpuContext, dets, traceImage, sizes, octave, hessianThreshold, sampleStep);
}

void SURFGPU::Find(const std::vector<gls::mtl_image_2d<float>::unique_ptr>& dets,
                       const std::vector<gls::mtl_image_2d<float>::unique_ptr>& traces, const std::vector<int>& sizes,
                       const std::vector<int>& sampleSteps, const std::vector<int>& middleIndices,
                       std::vector<KeyPoint>* keypoints, int nOctaveLayers, float hessianThreshold) const {
    int M = (int)middleIndices.size();
    LOG_INFO(TAG) << "enqueueing " << M << " findMaximaInLayer" << std::endl;
    for (int i = 0; i < M; i++) {
        const int layer = middleIndices[i];
        const int octave = i / nOctaveLayers;

        const std::array<const gls::mtl_image_2d<float>*, 3> detImages = {dets[layer - 1].get(), dets[layer].get(),
                                                                         dets[layer + 1].get()};

        const auto traceImage = traces[layer].get();

        findMaximaInLayer(detImages, *traceImage, {sizes[layer - 1], sizes[layer], sizes[layer + 1]}, octave,
                          hessianThreshold, sampleSteps[layer]);
    }

    // Collect results
    // FIXME: make a proper accessor for _keyPointsBuffer
    const auto keyPointMaxima = (KeyPointMaxima*)_findMaximaInLayer._keyPointsBuffer->contents();
//    const auto keyPointMaxima = (KeyPointMaxima*)cl::enqueueMapBuffer(
//        _keyPointsBuffer, true, CL_MAP_READ | CL_MAP_WRITE, 0, sizeof(KeyPointMaxima));

    LOG_INFO(TAG) << "keyPointMaxima: " << keyPointMaxima->count << std::endl;
    std::span<KeyPoint> newElements(keyPointMaxima->keyPoints,
                                    std::min(keyPointMaxima->count, KeyPointMaxima::MaxCount));
    keypoints->insert(end(*keypoints), begin(newElements), end(newElements));

    // Reset count
    keyPointMaxima->count = 0;

//    cl::enqueueUnmapMemObject(_keyPointsBuffer, (void*)keyPointMaxima);
}

struct KeypointGreater {
    inline bool operator()(const KeyPoint& kp1, const KeyPoint& kp2) const {
        if (kp1.response > kp2.response) return true;
        if (kp1.response < kp2.response) return false;
        if (kp1.size > kp2.size) return true;
        if (kp1.size < kp2.size) return false;
        if (kp1.octave > kp2.octave) return true;
        if (kp1.octave < kp2.octave) return false;
        if (kp1.pt.y < kp2.pt.y) return false;
        if (kp1.pt.y > kp2.pt.y) return true;
        return kp1.pt.x < kp2.pt.x;
    }
};

void SURFGPU::fastHessianDetector(const std::array<gls::mtl_image_2d<float>::unique_ptr, 4>& sum,
                                  std::vector<KeyPoint>* keypoints, int nOctaves, int nOctaveLayers,
                                  float hessianThreshold) const {
    int nTotalLayers = (nOctaveLayers + 2) * nOctaves;
    int nMiddleLayers = nOctaveLayers * nOctaves;

    std::vector<int> sizes(nTotalLayers);
    std::vector<int> sampleSteps(nTotalLayers);
    std::vector<int> middleIndices(nMiddleLayers);

    // Calculate properties of each layer
    int index = 0, middleIndex = 0, step = SAMPLE_STEP0;

    for (int octave = 0; octave < nOctaves; octave++) {
        for (int layer = 0; layer < nOctaveLayers + 2; layer++) {
            sizes[index] = (SURF_HAAR_SIZE0 + SURF_HAAR_SIZE_INC * layer) << octave;
            sampleSteps[index] = step;

            if (0 < layer && layer <= nOctaveLayers) {
                middleIndices[middleIndex++] = index;
            }
            index++;
        }
        step *= 2;
    }

    auto t_start = std::chrono::high_resolution_clock::now();

#if USE_GPU_HESSIAN_DETECTOR
    // Calculate hessian determinant and trace samples in each layer
    Build(sum, sizes, sampleSteps, _dets, _traces);

    // Find maxima in the determinant of the hessian
    Find(_dets, _traces, sizes, sampleSteps, middleIndices, keypoints, nOctaveLayers, hessianThreshold);
#else
    const auto sumCpu = sum[0]->mapImage();
    std::vector<gls::image<float>::unique_ptr> detsCpu;
    for (auto& mtl_det : _dets) {
        detsCpu.push_back(mtl_det->mapImage());
    }
    std::vector<gls::image<float>::unique_ptr> tracesCpu;
    for (auto& mtl_trace : _traces) {
        tracesCpu.push_back(mtl_trace->mapImage());
    }

    // Calculate hessian determinant and trace samples in each layer
    SURFBuild(*sumCpu, sizes, sampleSteps, detsCpu, tracesCpu, nOctaves, nOctaveLayers);

    // Find maxima in the determinant of the hessian
    SURFFind(*sumCpu, detsCpu, tracesCpu, sizes, sampleSteps, middleIndices, keypoints, nOctaveLayers, hessianThreshold);
#endif

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    LOG_INFO(TAG) << "Features Finding Time: " << elapsed_time_ms << std::endl;

    sort(keypoints->begin(), keypoints->end(), KeypointGreater());
}

#ifdef __APPLE__
typedef std::chrono::steady_clock::time_point time_point;
#else
typedef std::chrono::system_clock::time_point time_point;
#endif

double timeDiff(time_point t_start, time_point t_end) {
    return std::chrono::duration<double, std::milli>(t_end - t_start).count();
}

static bool keypointsAvailable(const std::vector<size_t>& kptIndices, const std::vector<size_t>& kptSizes) {
    for (int i = 0; i < kptIndices.size(); i++) {
        if (kptIndices[i] < kptSizes[i]) {
            return true;
        }
    }
    return false;
}

static int maxKeypointIndex(const std::vector<size_t>& kptIndices,
                            const std::vector<std::unique_ptr<std::vector<KeyPoint>>>& allKeypoints) {
    int maxIndex = -1;
    for (int i = 0; i < kptIndices.size(); i++) {
        if (kptIndices[i] < allKeypoints[i]->size()) {
            maxIndex = i;
        }
    }
    for (int i = maxIndex + 1; i < kptIndices.size(); i++) {
        if (kptIndices[i] < allKeypoints[i]->size() &&
            KeypointGreater()((*allKeypoints[i])[kptIndices[i]], (*allKeypoints[maxIndex])[kptIndices[maxIndex]])) {
            maxIndex = i;
        }
    }
    return maxIndex;
}

// Merge individually sorted keypoint vectors into a single keypoint vector
static void mergeKeypoints(const std::vector<std::unique_ptr<std::vector<KeyPoint>>>& allKeypoints,
                           std::vector<KeyPoint>* keypoints,
                           const std::vector<gls::image<float>::unique_ptr>& allDescriptors,
                           gls::image<float>::unique_ptr* descriptors) {
    // Find out how many keypoints we have
    int keypointsCount = 0;
    for (const auto& kps : allKeypoints) {
        keypointsCount += kps->size();
    }

    // Make sure we have corresponding descriptors for all keypoints
    if (descriptors) {
        assert(allKeypoints.size() == allDescriptors.size());

        int descriptorsCount = 0;
        for (const auto& d : allDescriptors) {
            descriptorsCount += d->height;
        }
        assert(keypointsCount == descriptorsCount);
    }

    // Allocate space for the result
    keypoints->clear();
    keypoints->resize(keypointsCount);

    if (descriptors != nullptr) {
        *descriptors = std::make_unique<gls::image<float>>(64, keypointsCount);
    }

    std::vector<size_t> kptIndices(allKeypoints.size());
    std::vector<size_t> kptSizes(allKeypoints.size());
    for (int i = 0; i < allKeypoints.size(); i++) {
        kptIndices[i] = 0;
        kptSizes[i] = allKeypoints[i]->size();
    }

    int outIndex = 0;
    while (keypointsAvailable(kptIndices, kptSizes)) {
        assert(outIndex < keypointsCount);

        // Find max keypoint index
        int maxIndex = maxKeypointIndex(kptIndices, allKeypoints);

        // Copy keypoint to output
        (*keypoints)[outIndex] = (*allKeypoints[maxIndex])[kptIndices[maxIndex]];

        // Copy descriptor to output
        if (descriptors != nullptr) {
            float* outPtr = (**descriptors)[outIndex];
            float* descPtr = (*allDescriptors[maxIndex])[(int)kptIndices[maxIndex]];

            memcpy(outPtr, descPtr, 64 * sizeof(float));
        }
        kptIndices[maxIndex]++;
        outIndex++;
    }

    assert(outIndex == keypointsCount);
}

void SURFGPU::detectAndCompute(const gls::image<float>& img, std::vector<KeyPoint>* keypoints,
                               gls::image<float>::unique_ptr* descriptors, gls::size sections) const {
    std::vector<gls::rectangle> tiles(sections.width * sections.height);

    const int tile_width = img.width / sections.width;
    const int tile_height = img.height / sections.height;

    LOG_INFO(TAG) << "Tile size: " << tile_width << " x " << tile_height << std::endl;

    // TODO: Add a skirt overlap between tiles
    int y_pos = 0;
    for (int j = 0; j < sections.height; j++) {
        int x_pos = 0;
        for (int i = 0; i < sections.width; i++) {
            tiles[j * sections.width + i] = gls::rectangle({x_pos, y_pos, tile_width, tile_height});
            x_pos += tile_width;
        }
        y_pos += tile_height;
    }

    std::vector<gls::image<float>::unique_ptr> allDescriptors;
    std::vector<std::unique_ptr<std::vector<KeyPoint>>> allKeypoints;

    auto sum = sumImageStack<float>(_gpuContext, tile_width + 1, tile_height + 1);

    for (const auto& tile : tiles) {
        const auto tileImage = gls::image<float>(img, tile);

        integral(tileImage, sum);

        auto tileKeypoints = std::make_unique<std::vector<KeyPoint>>();

        fastHessianDetector(sum, tileKeypoints.get(), _nOctaves, _nOctaveLayers, _hessianThreshold);

        // Limit the max number of feature points
        if (tileKeypoints->size() > _max_features) {
            LOG_INFO(TAG) << "detectAndCompute - dropping: " << (int)tileKeypoints->size() - _max_features
                          << " features out of " << (int)tileKeypoints->size() << std::endl;
            tileKeypoints->erase(tileKeypoints->begin() + _max_features, tileKeypoints->end());
        }

        int N = (int)tileKeypoints->size();

        LOG_INFO(TAG) << "tileKeypoints: " << N << std::endl;

        auto tileDescriptors = descriptors != nullptr ? std::make_unique<gls::image<float>>(64, N) : nullptr;

        auto t_start_descriptor = std::chrono::high_resolution_clock::now();

        const auto integralSumCpu = sum[0]->mapImage();

        // we call SURFInvoker in any case, even if we do not need descriptors,
        // since it computes orientation of each feature.
        descriptor(tileImage, *integralSumCpu, tileKeypoints.get(),
                   descriptors != nullptr ? tileDescriptors.get() : nullptr);

#if DEBUG_RECONSTRUCTED_IMAGE
        static int count = 0;
        gls::image<gls::luma_pixel> reconstructed(integralSumCpu->width - 1, integralSumCpu->height - 1);
        reconstructed.apply([&integralSumCpu](gls::luma_pixel* p, int x, int y) {
            float value = integralRectangle((*integralSumCpu)[y + 1][x + 1], (*integralSumCpu)[y + 1][x],
                                            (*integralSumCpu)[y][x + 1], (*integralSumCpu)[y][x]);
            *p = std::clamp((int)(255 * value), 0, 255);
        });
        reconstructed.write_png_file("/Users/fabio/reconstructed" + std::to_string(count++) + ".png");
#endif
        // sum[0]->unmapImage(integralSumCpu);

        // Translate tile keypoints to their full image locations
        for (auto& kp : *tileKeypoints) {
            kp.pt += Point2f(tile.x, tile.y);
        }
        allKeypoints.push_back(std::move(tileKeypoints));

        if (descriptors != nullptr) {
            allDescriptors.push_back(std::move(tileDescriptors));
        }

        auto t_end_descriptor = std::chrono::high_resolution_clock::now();
        LOG_INFO(TAG) << "--> descriptor Time: " << timeDiff(t_start_descriptor, t_end_descriptor) << std::endl;
    }

    mergeKeypoints(allKeypoints, keypoints, allDescriptors, descriptors);

    LOG_INFO(TAG) << "Collected " << keypoints->size() << " keypoints and " << (**descriptors).height << " descriptors"
                  << std::endl;
}

std::vector<std::pair<Point2f, Point2f>> SURF::detection(MetalContext* cLContext, const gls::image<float>& image1,
                                                         const gls::image<float>& image2) {
    auto t_start = std::chrono::high_resolution_clock::now();

    auto surf = SURF::makeInstance(cLContext, image1.width, image1.height, /*max_features=*/1500, /*nOctaves=*/4,
                                   /*nOctaveLayers=*/2, /*hessianThreshold=*/0.02);

    auto t_surf = std::chrono::high_resolution_clock::now();
    LOG_INFO(TAG) << "--> SURF Creation Time: " << timeDiff(t_start, t_surf) << std::endl;

    auto keypoints1 = std::make_unique<std::vector<KeyPoint>>();
    auto keypoints2 = std::make_unique<std::vector<KeyPoint>>();
    gls::image<float>::unique_ptr descriptor1, descriptor2;

    surf->detectAndCompute(image1, keypoints1.get(), &descriptor1);
    surf->detectAndCompute(image2, keypoints2.get(), &descriptor2);

    auto t_detect = std::chrono::high_resolution_clock::now();
    LOG_INFO(TAG) << "--> detectAndCompute Time: " << timeDiff(t_surf, t_detect) << std::endl;

    LOG_INFO(TAG) << " ---------- \n Detected feature points: " << keypoints1->size() << ", " << keypoints2->size()
                  << std::endl;

    // (4) Match feature points
    std::vector<DMatch> matchedPoints = surf->matchKeyPoints(*descriptor1, *descriptor2);

    auto t_match = std::chrono::high_resolution_clock::now();
    LOG_INFO(TAG) << "--> Keypoint Matching: " << timeDiff(t_detect, t_match) << std::endl;

    auto t_sort = std::chrono::high_resolution_clock::now();
    LOG_INFO(TAG) << "--> Keypoint Sorting: " << timeDiff(t_match, t_sort) << std::endl;

    // Convert to Point2D format
    std::vector<std::pair<Point2f, Point2f>> result(matchedPoints.size());
    for (int i = 0; i < matchedPoints.size(); i++) {
        result[i] = std::pair{(*keypoints1)[matchedPoints[i].queryIdx].pt, (*keypoints2)[matchedPoints[i].trainIdx].pt};
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    LOG_INFO(TAG) << "--> Keypoint Matching & Sorting Time: " << timeDiff(t_detect, t_end) << std::endl;

    double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    LOG_INFO(TAG) << "--> Features Finding Time: " << elapsed_time_ms << std::endl;

    return result;
}

}  // namespace gls
