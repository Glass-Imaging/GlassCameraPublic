// Copyright (c) 2021-2023 Glass Imaging Inc.
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

#include <metal_stdlib>
using namespace metal;

inline void write_imagef(texture2d<float, access::write> image, int2 coord, float4 value) {
    image.write(value, static_cast<uint2>(coord));
}

inline float4 read_imagef(texture2d<float> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

inline float4 read_imagef(texture2d<float> image, sampler s, float2 coord) {
    return image.sample(s, coord);
}

template <typename T, access a>
inline int2 get_image_dim(texture2d<T, a> image) {
    return int2(image.get_width(), image.get_height());
}

#define get_global_id(i) index[i]

#define get_image_height(sourceImage) sourceImage.get_height()
#define get_image_width(sourceImage) sourceImage.get_width()

struct _int8 {
    union {
        struct {
            int4 lo;
            int4 hi;
        };
        array<int, 8> v;
    };

    _int8(int val) : lo(val), hi(val) { }
};

struct _float8 {
    union {
        struct {
            float4 lo;
            float4 hi;
        };
        array<float, 8> v;
    };

    _float8(float val) : lo(val), hi(val) { }
};

// TODO: generate the wavelet parameters dynamically in the shader
typedef struct SurfHF {
    _int8 p_dx[2];
    _int8 p_dy[2];
    _int8 p_dxy[4];
} SurfHF;

float integralRectangle(float topRight, float topLeft, float bottomRight, float bottomLeft) {
    // Use Signed Offset Pixel Representation to improve Integral Image precision, see Integral Image code below
    return 0.5 + (topRight - topLeft) - (bottomRight - bottomLeft);
}

/*
 NOTE: calcHaarPatternDx and calcHaarPatternDy have been hand optimized to avoid loading data from repeated offsets,
       and the redundant offsets themselves have been removed from the SurfHF struct.
 */
float calcHaarPatternDx(texture2d<float> inputImage, const int2 p, constant _int8 *dp, float w) {
    float r02 = read_imagef(inputImage, p + dp[0].lo.lo).x;
    float r07 = read_imagef(inputImage, p + dp[0].lo.hi).x;
    float r32 = read_imagef(inputImage, p + dp[0].hi.lo).x;
    float r37 = read_imagef(inputImage, p + dp[0].hi.hi).x;

    float r62 = read_imagef(inputImage, p + dp[1].lo.lo).x;
    float r67 = read_imagef(inputImage, p + dp[1].lo.hi).x;

    float r92 = read_imagef(inputImage, p + dp[1].hi.lo).x;
    float r97 = read_imagef(inputImage, p + dp[1].hi.hi).x;

    return w * (integralRectangle(r97, r92, r07, r02) - 3 * integralRectangle(r67, r62, r37, r32));
}

float calcHaarPatternDy(texture2d<float> inputImage, const int2 p, constant _int8 *dp, float w) {
    float r20 = read_imagef(inputImage, p + dp[0].lo.lo).x;
    float r23 = read_imagef(inputImage, p + dp[0].lo.hi).x;
    float r26 = read_imagef(inputImage, p + dp[1].lo.lo).x;
    float r29 = read_imagef(inputImage, p + dp[1].hi.lo).x;

    float r70 = read_imagef(inputImage, p + dp[0].hi.lo).x;
    float r73 = read_imagef(inputImage, p + dp[0].hi.hi).x;
    float r76 = read_imagef(inputImage, p + dp[1].lo.hi).x;
    float r79 = read_imagef(inputImage, p + dp[1].hi.hi).x;

    return w * (integralRectangle(r79, r70, r29, r20) - 3 * integralRectangle(r76, r73, r26, r23));
}

float calcHaarPatternDxy(texture2d<float> inputImage, const int2 p, constant _int8 *dp, float w) {
    const float w4[4] = { w, -w, -w, w };
    float d = 0;
#pragma unroll
    for (int k = 0; k < 4; k++) {
        constant _int8* v = &dp[k];

        float p0 = read_imagef(inputImage, p + v->lo.lo /* p0 */).x;
        float p1 = read_imagef(inputImage, p + v->lo.hi /* p1 */).x;
        float p2 = read_imagef(inputImage, p + v->hi.lo /* p2 */).x;
        float p3 = read_imagef(inputImage, p + v->hi.hi /* p3 */).x;

        d += w4[k] * integralRectangle(p0, p1, p2, p3);
    }
    return d;
}

kernel void calcDetAndTrace(texture2d<float> sumImage                   [[texture(0)]],
                            texture2d<float, access::write> detImage    [[texture(1)]],
                            texture2d<float, access::write> traceImage  [[texture(2)]],
                            constant int& sampleStep                    [[buffer(3)]],
                            constant float2& w                          [[buffer(4)]],
                            constant int2& margin                       [[buffer(5)]],
                            constant SurfHF *surfHFData                 [[buffer(6)]],
                            uint2 index                                 [[thread_position_in_grid]])
{
    const int2 imageCoordinates = int2(index);
    const int2 p = imageCoordinates * sampleStep;

    const float dx = calcHaarPatternDx(sumImage, p, surfHFData->p_dx, w.x);
    const float dy = calcHaarPatternDy(sumImage, p, surfHFData->p_dy, w.x);
    const float dxy = calcHaarPatternDxy(sumImage, p, surfHFData->p_dxy, w.y);

    write_imagef(detImage, imageCoordinates + margin, dx * dy - 0.81f * dxy * dxy);
    write_imagef(traceImage, imageCoordinates + margin, dx + dy);
}

float2 detAndTrace(texture2d<float> sumImage, const int2 p, constant SurfHF *surfHFData, const float2 w) {
    const float dx = calcHaarPatternDx(sumImage, p, surfHFData[0].p_dx, w.x);
    const float dy = calcHaarPatternDy(sumImage, p, surfHFData[0].p_dy, w.x);
    const float dxy = calcHaarPatternDxy(sumImage, p, surfHFData[0].p_dxy, w.y);
    return float2(dx * dy - 0.81f * dxy * dxy, dx + dy);
}

kernel void calcDetAndTrace4(texture2d<float> sumImage                      [[texture(0)]],
                             texture2d<float, access::write> detImage0      [[texture(1)]],
                             texture2d<float, access::write> detImage1      [[texture(2)]],
                             texture2d<float, access::write> detImage2      [[texture(3)]],
                             texture2d<float, access::write> detImage3      [[texture(4)]],
                             texture2d<float, access::write> traceImage0    [[texture(5)]],
                             texture2d<float, access::write> traceImage1    [[texture(6)]],
                             texture2d<float, access::write> traceImage2    [[texture(7)]],
                             texture2d<float, access::write> traceImage3    [[texture(8)]],
                             constant int& sampleStep                       [[buffer(9)]],
                             constant _float8& w                            [[buffer(10)]],
                             constant int4& margin                          [[buffer(11)]],
                             constant SurfHF *surfHFData                    [[buffer(12)]],
                             uint2 index                                    [[thread_position_in_grid]]) {
    const int2 imageCoordinates = int2(index);
    const int2 p = imageCoordinates * sampleStep;

    float2 detAndTrace0 = detAndTrace(sumImage, p, &surfHFData[0], w.lo.lo);
    float2 detAndTrace1 = detAndTrace(sumImage, p, &surfHFData[1], w.lo.hi);
    float2 detAndTrace2 = detAndTrace(sumImage, p, &surfHFData[2], w.hi.lo);
    float2 detAndTrace3 = detAndTrace(sumImage, p, &surfHFData[3], w.hi.hi);

    write_imagef(detImage0, imageCoordinates + margin.x, float4(detAndTrace0.x, 0, 0, 0));
    write_imagef(traceImage0, imageCoordinates + margin.x, float4(detAndTrace0.y, 0, 0, 0));

    write_imagef(detImage1, imageCoordinates + margin.y, float4(detAndTrace1.x, 0, 0, 0));
    write_imagef(traceImage1, imageCoordinates + margin.y, float4(detAndTrace1.y, 0, 0, 0));

    write_imagef(detImage2, imageCoordinates + margin.z, float4(detAndTrace2.x, 0, 0, 0));
    write_imagef(traceImage2, imageCoordinates + margin.z, float4(detAndTrace2.y, 0, 0, 0));

    write_imagef(detImage3, imageCoordinates + margin.w, float4(detAndTrace3.x, 0, 0, 0));
    write_imagef(traceImage3, imageCoordinates + margin.w, float4(detAndTrace3.y, 0, 0, 0));
}

inline float determinant(const float3 A0, const float3 A1, const float3 A2) {
    // The cross product computes the 2x2 sub-determinants
    return dot(A0, cross(A1, A2));
}

// Simple Cramer's rule solver
inline float3 solve3x3(const float3 A[3], const float3 B) {
    float det = determinant(A[0], A[1], A[2]);
    if (det == 0) {
        return 0;
    }
    return (float3) (
        determinant(B,    A[1], A[2]),
        determinant(A[0], B,    A[2]),
        determinant(A[0], A[1], B)
    ) / det;
}

typedef struct KeyPoint {
    struct {
        float x, y;
    } pt;
    float size;
    float angle;
    float response;
    int octave;
    int class_id;
} KeyPoint;

#define N9(_idx, _off_y, _off_x) read_imagef(detImage ## _idx, p + int2(_off_x, _off_y)).x

bool interpolateKeypoint(texture2d<float> detImage0, texture2d<float> detImage1, texture2d<float> detImage2,
                         int2 p, int dx, int dy, int ds, thread KeyPoint* kpt) {
    float3 B = {
        -(N9(1, 0, 1) - N9(1,  0, -1)) / 2, // Negative 1st deriv with respect to x
        -(N9(1, 1, 0) - N9(1, -1,  0)) / 2, // Negative 1st deriv with respect to y
        -(N9(2, 0, 0) - N9(0,  0,  0)) / 2  // Negative 1st deriv with respect to s
    };
    float3 A[3] = {
        {  N9(1,  0, -1) - 2 * N9(1,  0,  0) + N9(1,  0, 1),                           // 2nd deriv x, x
          (N9(1,  1,  1) -     N9(1,  1, -1) - N9(1, -1, 1) + N9(1, -1, -1)) / 4,      // 2nd deriv x, y
          (N9(2,  0,  1) -     N9(2,  0, -1) - N9(0,  0, 1) + N9(0,  0, -1)) / 4 },    // 2nd deriv x, s
        { (N9(1,  1,  1) -     N9(1,  1, -1) - N9(1, -1, 1) + N9(1, -1, -1)) / 4,      // 2nd deriv x, y
           N9(1, -1,  0) - 2 * N9(1,  0,  0) + N9(1,  1, 0),                           // 2nd deriv y, y
          (N9(2,  1,  0) -     N9(2, -1,  0) - N9(0,  1, 0) + N9(0, -1,  0)) / 4 },    // 2nd deriv y, s
        { (N9(2,  0,  1) -     N9(2,  0, -1) - N9(0,  0, 1) + N9(0,  0, -1)) / 4,      // 2nd deriv x, s
          (N9(2,  1,  0) -     N9(2, -1,  0) - N9(0,  1, 0) + N9(0, -1,  0)) / 4,      // 2nd deriv y, s
           N9(0,  0,  0) - 2 * N9(1,  0,  0) + N9(2,  0, 0) }                          // 2nd deriv s, s
    };
    float3 x = solve3x3(A, B);

    if (!all(x == 0) && all(fabs(x) <= 1)) {
        kpt->pt.x += x.x * dx;
        kpt->pt.y += x.y * dy;
        kpt->size = (float) rint(kpt->size + x.z * ds);
        return true;
    }
    return false;
}

#define KeyPointMaxima_MaxCount 64000
typedef struct KeyPointMaxima {
    int count;
    KeyPoint keyPoints[KeyPointMaxima_MaxCount];
} KeyPointMaxima;

kernel void findMaximaInLayer(texture2d<float> detImage0        [[texture(0)]],
                              texture2d<float> detImage1        [[texture(1)]],
                              texture2d<float> detImage2        [[texture(2)]],
                              texture2d<float> traceImage       [[texture(3)]],
                              constant int3& sizes              [[buffer(4)]],
                              device KeyPointMaxima* keypoints  [[buffer(5)]],
                              constant int& margin              [[buffer(6)]],
                              constant int& octave              [[buffer(7)]],
                              constant float& hessianThreshold  [[buffer(8)]],
                              constant int& sampleStep          [[buffer(9)]],
                              uint2 index                       [[thread_position_in_grid]]) {
    const int2 p = int2(index) + margin;

    const int size = sizes.y;

    const float val0 = N9(1, 0, 0);

    if (val0 > hessianThreshold) {
        /* Coordinates for the start of the wavelet in the sum image. There
           is some integer division involved, so don't try to simplify this
           (cancel out sampleStep) without checking the result is the same */
        int sum_y = sampleStep * (p.y - (size / 2) / sampleStep);
        int sum_x = sampleStep * (p.x - (size / 2) / sampleStep);

        /* The 3x3x3 neighbouring samples around the maxima.
           The maxima is included at N9[1][0][0] */

        /* Non-maxima suppression. val0 is at N9[1][0][0] */
        if (val0 > N9(0, -1, -1) && val0 > N9(0, -1, 0) && val0 > N9(0, -1, 1) &&
            val0 > N9(0,  0, -1) && val0 > N9(0,  0, 0) && val0 > N9(0,  0, 1) &&
            val0 > N9(0,  1, -1) && val0 > N9(0,  1, 0) && val0 > N9(0,  1, 1) &&
            val0 > N9(1, -1, -1) && val0 > N9(1, -1, 0) && val0 > N9(1, -1, 1) &&
            val0 > N9(1,  0, -1)                        && val0 > N9(1,  0, 1) &&
            val0 > N9(1,  1, -1) && val0 > N9(1,  1, 0) && val0 > N9(1,  1, 1) &&
            val0 > N9(2, -1, -1) && val0 > N9(2, -1, 0) && val0 > N9(2, -1, 1) &&
            val0 > N9(2,  0, -1) && val0 > N9(2,  0, 0) && val0 > N9(2,  0, 1) &&
            val0 > N9(2,  1, -1) && val0 > N9(2,  1, 0) && val0 > N9(2,  1, 1))
        {
            /* Calculate the wavelet center coordinates for the maxima */
            float center_y = sum_y + (size - 1) * 0.5f;
            float center_x = sum_x + (size - 1) * 0.5f;
            KeyPoint kpt = {{center_x, center_y}, (float)sizes.y, -1, val0, octave, read_imagef(traceImage, p).x > 0 };

            /* Interpolate maxima location within the 3x3x3 neighbourhood  */
            int ds = size - sizes.x;
            int interp_ok = interpolateKeypoint(detImage0, detImage1, detImage2, p, sampleStep, sampleStep, ds, &kpt);

            /* Sometimes the interpolation step gives a negative size etc. */
            if (interp_ok) {
                // int ind = atomic_inc(&keypoints->count);
                // TODO: verify that this is correct
                int ind = atomic_fetch_add_explicit((device atomic<int>*) &keypoints->count, 1, memory_order_relaxed);
                if (ind < KeyPointMaxima_MaxCount) {
                    keypoints->keyPoints[ind] = kpt;
                }
            }
        }
    }
}

// Integral Image

#define LOCAL_SUM_SIZE      8U

kernel void integral_sum_cols_image(texture2d<float> sourceImage    [[texture(0)]],
                                    device float *buf_ptr           [[buffer(1)]],
                                    constant int& buf_width         [[buffer(2)]],
                                    uint lid                        [[thread_position_in_threadgroup]],
                                    uint gid                        [[threadgroup_position_in_grid]],
                                    uint x                          [[thread_position_in_grid]]) {
    threadgroup float lm_sum[LOCAL_SUM_SIZE][LOCAL_SUM_SIZE];

    float accum = 0;
    for (uint y = 0; y < get_image_height(sourceImage); y += LOCAL_SUM_SIZE) {
#pragma unroll
        for (uint yin = 0; yin < LOCAL_SUM_SIZE; yin++) {
            // Use Signed Offset Pixel Representation to improve Integral Image precision
            // See: Hensley et al.: "Fast Summed-Area Table Generation and its Applications".
            accum += read_imagef(sourceImage, int2(x, y + yin)).x - 0.5;
            lm_sum[yin][lid] = accum;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);

        int buf_index = buf_width * LOCAL_SUM_SIZE * gid + (lid + y);
#pragma unroll
        for (uint yin = 0; yin < LOCAL_SUM_SIZE; yin++, buf_index += buf_width) {
            buf_ptr[buf_index] = lm_sum[lid][yin];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}

kernel void integral_sum_rows_image(constant float* buf_ptr                     [[buffer(0)]],
                                    constant int& buf_width                     [[buffer(1)]],
                                    texture2d<float, access::write> sumImage0   [[texture(2)]],
                                    texture2d<float, access::write> sumImage1   [[texture(3)]],
                                    texture2d<float, access::write> sumImage2   [[texture(4)]],
                                    texture2d<float, access::write> sumImage3   [[texture(5)]],
                                    uint lid                                    [[thread_position_in_threadgroup]],
                                    uint gid                                    [[threadgroup_position_in_grid]],
                                    uint gs                                     [[threads_per_grid]],
                                    uint x                                      [[thread_position_in_grid]]) {
    uint dst_width = get_image_width(sumImage0);
    uint dst_height = get_image_height(sumImage0);

    threadgroup float lm_sum[LOCAL_SUM_SIZE][LOCAL_SUM_SIZE];

    for (uint xin = x; xin < dst_width; xin += gs) {
        write_imagef(sumImage0, int2(xin, 0), 0);

        if ((xin & 1) == 0) {
            write_imagef(sumImage1, int2(xin / 2, 0), 0);
        }
        if ((xin & 3) == 0) {
            write_imagef(sumImage2, int2(xin / 4, 0), 0);
        }
        if ((xin & 7) == 0) {
            write_imagef(sumImage3, int2(xin / 8, 0), 0);
        }
    }

    if (x < dst_height - 1) {
        write_imagef(sumImage0, int2(0, x + 1), 0);

        if (((x + 1) & 1) == 0) {
            write_imagef(sumImage1, int2(0, (x + 1) / 2), 0);
        }
        if (((x + 1) & 3) == 0) {
            write_imagef(sumImage2, int2(0, (x + 1) / 4), 0);
        }
        if (((x + 1) & 7) == 0) {
            write_imagef(sumImage3, int2(0, (x + 1) / 8), 0);
        }
    }

    int buf_index = x;
    float accum = 0;
    for (uint y = 1; y < dst_width; y += LOCAL_SUM_SIZE) {
#pragma unroll
        for (uint yin = 0; yin < LOCAL_SUM_SIZE; yin++, buf_index += buf_width) {
            accum += buf_ptr[buf_index];
            lm_sum[yin][lid] = accum;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);

        if (y + lid < dst_width) {
            // FIXME: is dst_height > LOCAL_SUM_SIZE + 1?
            uint yin_max = min(dst_height - 1 - LOCAL_SUM_SIZE * gid, LOCAL_SUM_SIZE);
#pragma unroll
            for (uint yin = 0; yin < yin_max; yin++) {
                int2 outCoords = int2(y + lid, yin + LOCAL_SUM_SIZE * gid + 1);
                write_imagef(sumImage0, outCoords, lm_sum[lid][yin]);

                if (all((outCoords & 1) == 0)) {
                    write_imagef(sumImage1, outCoords / 2, lm_sum[lid][yin]);
                }
                if (all((outCoords & 3) == 0)) {
                    write_imagef(sumImage2, outCoords / 4, lm_sum[lid][yin]);
                }
                if (all((outCoords & 7) == 0)) {
                    write_imagef(sumImage3, outCoords / 8, lm_sum[lid][yin]);
                }
            }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}

float L2Norm(constant array<float4, 16>& p1, constant array<float4, 16>& p2) {
    float4 sum = 0;
    for (uint i = 0; i < p1.size(); i++) {
        float4 diff = p1[i] - p2[i];
        sum += diff * diff;
    }
    return sqrt(sum.x + sum.y + sum.z + sum.w);
}

typedef struct DMatch {
    uint queryIdx;  // query descriptor index
    uint trainIdx;  // train descriptor index
    float distance;
} DMatch;

#define MATCH_BLOCK_SIZE 24

kernel void matchKeyPoints(constant array<float4, 16>* descriptor1  [[buffer(0)]],
                           constant array<float4, 16>* descriptor2  [[buffer(1)]],
                           constant uint& descriptor2_height        [[buffer(2)]],
                           device DMatch* matchedPoints             [[buffer(3)]],
                           uint2 local_id                           [[thread_position_in_threadgroup]],
                           uint2 global_id                          [[thread_position_in_grid]]) {
    const uint lid = local_id.y;
    const uint i = global_id.x;

    constant array<float4, 16>& p1 = descriptor1[i];

    float distance_min = MAXFLOAT;
    uint j_min = 0;

    for (uint j = 0; j + lid < descriptor2_height; j += MATCH_BLOCK_SIZE) {
        constant array<float4, 16>& p2 = descriptor2[j + lid];
        // calculate distance
        float distance_t = L2Norm(p1, p2);
        if (distance_t < distance_min) {
            distance_min = distance_t;
            j_min = j + lid;
        }
    }

    threadgroup int j_min_v[MATCH_BLOCK_SIZE];
    threadgroup float distance_min_v[MATCH_BLOCK_SIZE];

    j_min_v[lid] = j_min;
    distance_min_v[lid] = distance_min;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    if (lid == 0) {
        distance_min = distance_min_v[0];
        j_min = j_min_v[0];

        for (uint i = 1; i < MATCH_BLOCK_SIZE; i++) {
            float distance_t = distance_min_v[i];
            if (distance_t < distance_min) {
                distance_min = distance_t;
                j_min = j_min_v[i];
            }
        }

        DMatch match = { i, j_min, distance_min };
        matchedPoints[i] = match;
    }
}

typedef struct {
    float3 m[3];
} Matrix3x3;

kernel void registerAndFuse(texture2d<float> fusedImage                     [[texture(0)]],
                            texture2d<float> inputImage                     [[texture(1)]],
                            texture2d<float, access::write> newFusedImage   [[texture(2)]],
                            constant Matrix3x3& homography                  [[buffer(3)]],
                            constant int& count                             [[buffer(4)]],
                            uint2 index                                     [[thread_position_in_grid]])
{
    const int2 imageCoordinates = int2(index);
    const float2 input_norm = 1.0 / float2(get_image_dim(newFusedImage));

    constexpr sampler linear_sampler(filter::linear);

    float3 p(imageCoordinates.x, imageCoordinates.y, 1);
    float u = dot(homography.m[0], p);
    float v = dot(homography.m[1], p);
    float w = dot(homography.m[2], p);
    float xx = u / w;
    float yy = v / w;

    float4 input0 = read_imagef(fusedImage, imageCoordinates);
    float4 input1 = read_imagef(inputImage, linear_sampler, (float2(xx, yy) + 0.5) * input_norm);

    write_imagef(newFusedImage, imageCoordinates, ((count - 1) * input0 + input1) / count);
}

kernel void registerImage(texture2d<float> inputImage                   [[texture(0)]],
                          texture2d<float, access::write> outputImage   [[texture(1)]],
                          constant Matrix3x3& homography                [[buffer(2)]],
                          uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = int2(index);
    const float2 input_norm = 1.0 / float2(get_image_dim(outputImage));

    constexpr sampler linear_sampler(filter::linear);

    float3 p(imageCoordinates.x, imageCoordinates.y, 1);
    float u = dot(homography.m[0], p);
    float v = dot(homography.m[1], p);
    float w = dot(homography.m[2], p);
    float xx = u / w;
    float yy = v / w;

    float4 input = read_imagef(inputImage, linear_sampler, (float2(xx, yy) + 0.5) * input_norm);

    write_imagef(outputImage, imageCoordinates, input);
}

kernel void registerBayerImage(texture2d<float> inputImage                   [[texture(0)]],
                               texture2d<float, access::write> outputImage   [[texture(1)]],
                               constant Matrix3x3& homography                [[buffer(2)]],
                               uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = int2(index);

    // Nearest neighbor interpolation of the individual color planes

    float3 p(imageCoordinates.x / 2, imageCoordinates.y / 2, 1);
    float u = dot(homography.m[0], p);
    float v = dot(homography.m[1], p);
    float w = dot(homography.m[2], p);
    float xx = u / w;
    float yy = v / w;

    float input = read_imagef(inputImage, 2 * int2(round(xx), round(yy)) + imageCoordinates % 2).x;

    write_imagef(outputImage, imageCoordinates, input);
}
