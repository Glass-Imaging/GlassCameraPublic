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

#include "demosaic_mtl.hpp"

#include <iostream>
#include <simd/simd.h>

typedef __fp16 float16_t;
typedef __fp16 half;

typedef __attribute__((__ext_vector_type__(2))) half simd_half2;
typedef __attribute__((__ext_vector_type__(3))) half simd_half3;
typedef __attribute__((__ext_vector_type__(4))) half simd_half4;

namespace simd {

typedef ::simd_half2 half2;
typedef ::simd_half3 half3;
typedef ::simd_half4 half4;

}

void scaleRawData(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_16>& rawImage,
                  gls::mtl_image_2d<gls::luma_pixel_float>* scaledRawImage, BayerPattern bayerPattern,
                  gls::Vector<4> scaleMul, float blackLevel) {

    auto kernel = Kernel<MTL::Texture*,     // rawImage
                         MTL::Texture*,     // scaledRawImage
                         int,               // bayerPattern
                         simd::half4,       // scaleMul
                         half               // blackLevel
                         >(mtlContext, "scaleRawData");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(scaledRawImage->width / 2, scaledRawImage->height / 2, 1),
           rawImage.texture(), scaledRawImage->texture(), bayerPattern,
           simd::half4 { (half) scaleMul[0], (half) scaleMul[1], (half) scaleMul[2], (half) scaleMul[3] },
           blackLevel);
}

void rawImageSobel(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                   gls::mtl_image_2d<gls::rgba_pixel_float>* gradientImage) {
    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*   // gradientImage
                         >(mtlContext, "rawImageSobel");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(gradientImage->width, gradientImage->height, 1),
           rawImage.texture(),
           gradientImage->texture());
}
