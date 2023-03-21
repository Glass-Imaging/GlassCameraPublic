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

void write_imagef(texture2d<float, access::write> image, int2 coord, float4 value) {
    image.write(value, static_cast<uint2>(coord));
}

float4 read_imagef(texture2d<float, access::read> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

void write_imageh(texture2d<half, access::write> image, int2 coord, half4 value) {
    image.write(value, static_cast<uint2>(coord));
}

half4 read_imageh(texture2d<half, access::read> image, int2 coord) {
    return image.read(static_cast<uint2>(coord));
}

// Work on one Quad (2x2) at a time
kernel void scaleRawData(texture2d<half, access::read> rawImage            [[texture(0)]],
                         texture2d<half, access::write> scaledRawImage     [[texture(1)]],
                         constant int& bayerPattern                         [[buffer(2)]],
                         constant half4& scaleMul                          [[buffer(3)]],
                         constant half& blackLevel                         [[buffer(4)]],
                         uint2 index                                        [[thread_position_in_grid]],
                         uint2 gridSize                                     [[threads_per_grid]])
{
    const int2 imageCoordinates = 2 * (int2) index;

    for (int c = 0; c < 4; c++) {
        int2 o = bayerOffsets[bayerPattern][c];
        write_imageh(scaledRawImage, imageCoordinates + o,
                     max(scaleMul[c] * (read_imageh(rawImage, imageCoordinates + o).x - blackLevel), 0.0h));
    }
}

constant half2 sobelKernel2D[3][3] = {
    { { 1,  1 }, { 0,  2 }, { -1,  1 } },
    { { 2,  0 }, { 0,  0 }, { -2,  0 } },
    { { 1, -1 }, { 0, -2 }, { -1, -1 } },
};

half2 sobel(texture2d<half, access::read> inputImage, int x, int y) {
    half2 value = 0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            half sample = read_imageh(inputImage, (int2) { x + i, y + j }).x;
            value += sobelKernel2D[j+1][i+1] * sample;
        }
    }
    return value / sqrt(4.5);
}

kernel void rawImageSobel(texture2d<half, access::read> inputImage         [[texture(0)]],
                          texture2d<half, access::write> gradientImage     [[texture(1)]],
                          uint2 index                                       [[thread_position_in_grid]])
{
    const int2 imageCoordinates = (int2) index;
    half2 gradient = sobel(inputImage, imageCoordinates.x, imageCoordinates.y);

    write_imageh(gradientImage, imageCoordinates, (half4) { gradient.x, gradient.y, abs(gradient.x), abs(gradient.y) });
}
