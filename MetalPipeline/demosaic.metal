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
kernel void scaleRawData(texture2d<float, access::read> rawImage            [[texture(0)]],
                         texture2d<float, access::write> scaledRawImage     [[texture(1)]],
                         constant int& bayerPattern                         [[buffer(2)]],
                         constant float4& scaleMul                          [[buffer(3)]],
                         constant float& blackLevel                         [[buffer(4)]],
                         uint2 index [[thread_position_in_grid]],
                         uint2 gridSize [[threads_per_grid]]) {
    for (int c = 0; c < 4; c++) {
        int2 off = bayerOffsets[bayerPattern][c];
        write_imagef(scaledRawImage, static_cast<int2>(2 * index) + off,
                     scaleMul[c] * (read_imagef(rawImage, static_cast<int2>(2 * index) + off) - blackLevel));
    }
}
