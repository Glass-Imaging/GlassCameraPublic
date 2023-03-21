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

std::vector<std::array<float, 3>> gaussianKernelBilinearWeights(float radius) {
    int kernelSize = (int)(ceil(2 * radius));
    if ((kernelSize % 2) == 0) {
        kernelSize++;
    }

    std::vector<float> weights(kernelSize * kernelSize);
    for (int y = -kernelSize / 2, i = 0; y <= kernelSize / 2; y++) {
        for (int x = -kernelSize / 2; x <= kernelSize / 2; x++, i++) {
            weights[i] = exp(-((float)(x * x + y * y) / (2 * radius * radius)));
        }
    }
//    std::cout << "Gaussian Kernel weights (" << weights.size() << "): " << std::endl;
//    for (const auto& w : weights) {
//        std::cout << std::setprecision(4) << std::scientific << w << ", ";
//    }
//    std::cout << std::endl;

    const int outWidth = kernelSize / 2 + 1;
    const int weightsCount = outWidth * outWidth;
    std::vector<std::array<float, 3>> weightsOut(weightsCount);
    KernelOptimizeBilinear2d(kernelSize, weights, &weightsOut);

//    std::cout << "Bilinear Gaussian Kernel weights and offsets (" << weightsOut.size() << "): " << std::endl;
//    for (const auto& [w, x, y] : weightsOut) {
//        std::cout << w << " @ (" << x << " : " << y << "), " << std::endl;
//    }

    return weightsOut;
}

void gaussianBlurSobelImage(MetalContext* mtlContext,
                            const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                            const gls::mtl_image_2d<gls::rgba_pixel_float>& sobelImage,
                            std::array<float, 2> rawNoiseModel, float radius1, float radius2,
                            gls::mtl_image_2d<gls::luma_alpha_pixel_float>* outputImage) {
    auto weightsOut1 = gaussianKernelBilinearWeights(radius1);
    auto weightsOut2 = gaussianKernelBilinearWeights(radius2);

    auto weightsBuffer1 = gls::Buffer(mtlContext->device(), weightsOut1.begin(), weightsOut1.end());
    auto weightsBuffer2 = gls::Buffer(mtlContext->device(), weightsOut2.begin(), weightsOut2.end());

    // Bind the kernel parameters
    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // sobelImage
                         int,            // samples1
                         MTL::Buffer*,   // weights1
                         int,            // samples2
                         MTL::Buffer*,   // weights2
                         simd::float2,   // rawVariance
                         MTL::Texture*   // outputImage
                         >(mtlContext, "sampledConvolutionSobel");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           rawImage.texture(),
           sobelImage.texture(),
           (int) weightsOut1.size(),
           weightsBuffer1.get(),
           (int) weightsOut2.size(),
           weightsBuffer2.get(),
           simd::float2 { rawNoiseModel[0], rawNoiseModel[1] },
           outputImage->texture());
}

void interpolateGreen(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                      const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                      gls::mtl_image_2d<gls::luma_pixel_float>* greenImage, BayerPattern bayerPattern,
                      gls::Vector<2> greenVariance) {
    // Bind the kernel parameters
    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // gradientImage
                         MTL::Texture*,  // greenImage
                         int,            // bayerPattern
                         simd::float2    // greenVariance
                         >(mtlContext, "interpolateGreen");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(greenImage->width, greenImage->height, 1), rawImage.texture(),
           gradientImage.texture(), greenImage->texture(), bayerPattern, simd::float2 {greenVariance[0], greenVariance[1]});
}

void interpolateRedBlue(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                        const gls::mtl_image_2d<gls::luma_pixel_float>& greenImage,
                        const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                        gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, BayerPattern bayerPattern,
                        gls::Vector<2> redVariance, gls::Vector<2> blueVariance) {
    // Bind the kernel parameters
    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // greenImage
                         MTL::Texture*,  // gradientImage
                         MTL::Texture*,  // rgbImage
                         int,            // bayerPattern
                         simd::float2,   // redVariance
                         simd::float2    // blueVariance
                         >(mtlContext, "interpolateRedBlue");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbImage->width / 2, rgbImage->height / 2, 1), rawImage.texture(),
           greenImage.texture(), gradientImage.texture(), rgbImage->texture(), bayerPattern,
           simd::float2 {redVariance[0], redVariance[1]}, simd::float2 {blueVariance[0], blueVariance[1]});
}

void interpolateRedBlueAtGreen(MetalContext* mtlContext,
                               const gls::mtl_image_2d<gls::rgba_pixel_float>& rgbImageIn,
                               const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                               gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImageOut, BayerPattern bayerPattern,
                               gls::Vector<2> redVariance, gls::Vector<2> blueVariance) {
    // Bind the kernel parameters
    auto kernel = Kernel<MTL::Texture*,  // rgbImageIn
                         MTL::Texture*,  // gradientImage
                         MTL::Texture*,  // rgbImageOut
                         int,            // bayerPattern
                         simd::float2,   // redVariance
                         simd::float2    // blueVariance
                         >(mtlContext, "interpolateRedBlueAtGreen");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbImageOut->width / 2, rgbImageOut->height / 2, 1),
           rgbImageIn.texture(), gradientImage.texture(), rgbImageOut->texture(), bayerPattern,
           simd::float2 {redVariance[0], redVariance[1]}, simd::float2 {blueVariance[0], blueVariance[1]});
}
