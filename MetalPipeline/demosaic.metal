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

#include <metal_stdlib>
using namespace metal;

enum BayerPattern {
    grbg = 0,
    gbrg = 1,
    rggb = 2,
    bggr = 3
};

constant const int2 bayerOffsets[4][4] = {
    { {1, 0}, {0, 0}, {0, 1}, {1, 1} }, // grbg
    { {0, 1}, {0, 0}, {1, 0}, {1, 1} }, // gbrg
    { {0, 0}, {0, 1}, {1, 1}, {1, 0} }, // rggb
    { {1, 1}, {0, 1}, {0, 0}, {1, 0} }  // bggr
};

constant half2 sobelKernel2D[3][3] = {
    { { 1,  1 }, { 0,  2 }, { -1,  1 } },
    { { 2,  0 }, { 0,  0 }, { -2,  0 } },
    { { 1, -1 }, { 0, -2 }, { -1, -1 } },
};

void write_imagef(texture2d<float, access::write> image, int2 coord, float4 value) {
    image.write(value, static_cast<uint2>(coord));
}

float4 read_imagef(texture2d<float> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

float4 read_imagef(texture2d<float> image, sampler s, float2 coord) {
    return image.sample(s, coord);
}

void write_imageh(texture2d<half, access::write> image, int2 coord, half4 value) {
    image.write(value, static_cast<uint2>(coord));
}

half4 read_imageh(texture2d<half> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

template <typename T, access a>
int2 get_image_dim(texture2d<T, a> image) {
    return int2(image.get_width(), image.get_height());
}

// Work on one Quad (2x2) at a time
kernel void scaleRawData(texture2d<half> rawImage                       [[texture(0)]],
                         texture2d<half, access::write> scaledRawImage  [[texture(1)]],
                         constant int& bayerPattern                     [[buffer(2)]],
                         constant half4& scaleMul                       [[buffer(3)]],
                         constant half& blackLevel                      [[buffer(4)]],
                         uint2 index                                    [[thread_position_in_grid]])
{
    const int2 imageCoordinates = 2 * (int2) index;

    for (int c = 0; c < 4; c++) {
        int2 o = bayerOffsets[bayerPattern][c];
        write_imageh(scaledRawImage, imageCoordinates + o,
                     max(scaleMul[c] * (read_imageh(rawImage, imageCoordinates + o).x - blackLevel), 0.0h));
    }
}

half2 sobel(texture2d<half> inputImage, int x, int y) {
    half2 value = 0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            half sample = read_imageh(inputImage, (int2) { x + i, y + j }).x;
            value += sobelKernel2D[j+1][i+1] * sample;
        }
    }
    return value / sqrt(4.5);
}

kernel void rawImageSobel(texture2d<half> inputImage                    [[texture(0)]],
                          texture2d<half, access::write> gradientImage  [[texture(1)]],
                          uint2 index                                   [[thread_position_in_grid]])
{
    const int2 imageCoordinates = (int2) index;
    half2 gradient = sobel(inputImage, imageCoordinates.x, imageCoordinates.y);

    write_imageh(gradientImage, imageCoordinates, (half4) { gradient.x, gradient.y, abs(gradient.x), abs(gradient.y) });
}

float4 sampledConvolution(texture2d<float> inputImage,
                          int2 imageCoordinates, float2 inputNorm,
                          int samples, constant float *weights) {
    const float2 inputPos = ((float2) imageCoordinates) * inputNorm;

    constexpr sampler linear_sampler(min_filter::linear,
                                     mag_filter::linear,
                                     mip_filter::linear);
    float4 sum = 0;
    float norm = 0;
    for (int i = 0; i < samples; i++) {
        float w = weights[3 * i + 0];
        sum += w * read_imagef(inputImage, linear_sampler, inputPos + ((float2) { weights[3 * i + 1], weights[3 * i + 2] } + 0.5) * inputNorm);
        norm += w;
    }
    return sum / norm;
}

kernel void sampledConvolutionSobel(texture2d<float> rawImage                   [[texture(0)]],
                                    texture2d<float> sobelImage                 [[texture(1)]],
                                    constant int& samples1                      [[buffer(2)]],
                                    constant float *weights1                    [[buffer(3)]],
                                    constant int& samples2                      [[buffer(4)]],
                                    constant float *weights2                    [[buffer(5)]],
                                    constant float2& rawVariance                [[buffer(6)]],
                                    texture2d<float, access::write> outputImage [[texture(7)]],
                                    uint2 index                                 [[thread_position_in_grid]]) {
    const int2 imageCoordinates = (int2) index;

    const float2 inputNorm = 1.0 / float2(get_image_dim(outputImage));
    float4 result = sampledConvolution(sobelImage, imageCoordinates, inputNorm, samples1, weights1);

    float sigma = sqrt(rawVariance.x + rawVariance.y * read_imagef(rawImage, imageCoordinates).x);
    if (length(result.xy) < 4 * sigma) {
        result = sampledConvolution(sobelImage, imageCoordinates, inputNorm, samples2, weights2);
    }

    float2 result2 = copysign(result.zw, result.xy);
    write_imagef(outputImage, imageCoordinates, (float4) { result2.x, result2.y, 0, 0});
}
