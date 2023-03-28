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

#include "float16.hpp"

void scaleRawData(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_16>& rawImage,
                  gls::mtl_image_2d<gls::luma_pixel_float>* scaledRawImage, BayerPattern bayerPattern,
                  gls::Vector<4> scaleMul, float blackLevel, float lensShadingCorrection) {

    auto kernel = Kernel<MTL::Texture*,     // rawImage
                         MTL::Texture*,     // scaledRawImage
                         int,               // bayerPattern
                         simd::half4,       // scaleMul
                         half,              // blackLevel
                         half               // lensShadingCorrection
                         >(mtlContext, "scaleRawData");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(scaledRawImage->width / 2, scaledRawImage->height / 2, 1),
           rawImage.texture(), scaledRawImage->texture(), bayerPattern,
           simd::half4 { (half) scaleMul[0], (half) scaleMul[1], (half) scaleMul[2], (half) scaleMul[3] },
           blackLevel, lensShadingCorrection);
}

void rawImageSobel(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                   gls::mtl_image_2d<gls::rgba_pixel_float>* gradientImage) {
    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*   // gradientImage
                         >(mtlContext, "rawImageSobel");

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

    const int outWidth = kernelSize / 2 + 1;
    const int weightsCount = outWidth * outWidth;
    std::vector<std::array<float, 3>> weightsOut(weightsCount);
    KernelOptimizeBilinear2d(kernelSize, weights, &weightsOut);

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

    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // sobelImage
                         int,            // samples1
                         MTL::Buffer*,   // weights1
                         int,            // samples2
                         MTL::Buffer*,   // weights2
                         simd::float2,   // rawVariance
                         MTL::Texture*   // outputImage
                         >(mtlContext, "sampledConvolutionSobel");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           rawImage.texture(), sobelImage.texture(),
           (int) weightsOut1.size(), weightsBuffer1.get(),
           (int) weightsOut2.size(), weightsBuffer2.get(),
           simd::float2 { rawNoiseModel[0], rawNoiseModel[1] },
           outputImage->texture());
}

void interpolateGreen(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                      const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                      gls::mtl_image_2d<gls::luma_pixel_float>* greenImage, BayerPattern bayerPattern,
                      gls::Vector<2> greenVariance) {

    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // gradientImage
                         MTL::Texture*,  // greenImage
                         int,            // bayerPattern
                         simd::float2    // greenVariance
                         >(mtlContext, "interpolateGreen");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(greenImage->width, greenImage->height, 1), rawImage.texture(),
           gradientImage.texture(), greenImage->texture(), bayerPattern, simd::float2 {greenVariance[0], greenVariance[1]});
}

void interpolateRedBlue(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                        const gls::mtl_image_2d<gls::luma_pixel_float>& greenImage,
                        const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                        gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, BayerPattern bayerPattern,
                        gls::Vector<2> redVariance, gls::Vector<2> blueVariance) {

    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // greenImage
                         MTL::Texture*,  // gradientImage
                         MTL::Texture*,  // rgbImage
                         int,            // bayerPattern
                         simd::float2,   // redVariance
                         simd::float2    // blueVariance
                         >(mtlContext, "interpolateRedBlue");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbImage->width / 2, rgbImage->height / 2, 1), rawImage.texture(),
           greenImage.texture(), gradientImage.texture(), rgbImage->texture(), bayerPattern,
           simd::float2 {redVariance[0], redVariance[1]}, simd::float2 {blueVariance[0], blueVariance[1]});
}

void interpolateRedBlueAtGreen(MetalContext* mtlContext,
                               const gls::mtl_image_2d<gls::rgba_pixel_float>& rgbImageIn,
                               const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                               gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImageOut, BayerPattern bayerPattern,
                               gls::Vector<2> redVariance, gls::Vector<2> blueVariance) {

    auto kernel = Kernel<MTL::Texture*,  // rgbImageIn
                         MTL::Texture*,  // gradientImage
                         MTL::Texture*,  // rgbImageOut
                         int,            // bayerPattern
                         simd::float2,   // redVariance
                         simd::float2    // blueVariance
                         >(mtlContext, "interpolateRedBlueAtGreen");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbImageOut->width / 2, rgbImageOut->height / 2, 1),
           rgbImageIn.texture(), gradientImage.texture(), rgbImageOut->texture(), bayerPattern,
           simd::float2 {redVariance[0], redVariance[1]}, simd::float2 {blueVariance[0], blueVariance[1]});
}

void blendHighlightsImage(MetalContext* mtlContext,
                          const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                          float clip, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

    auto kernel = Kernel<MTL::Texture*,  // inputImage
                         float,          // clip
                         MTL::Texture*   // outputImage
                         >(mtlContext, "blendHighlightsImage");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           inputImage.texture(), clip, outputImage->texture());
}

void transformImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& linearImage,
                    gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, const gls::Matrix<3, 3>& transform) {
    struct Matrix3x3 {
        simd::float3 m[3];
    } mtlTransform = {{{transform[0][0], transform[0][1], transform[0][2]},
                       {transform[1][0], transform[1][1], transform[1][2]},
                       {transform[2][0], transform[2][1], transform[2][2]}}};

    // Bind the kernel parameters
    auto kernel = Kernel<MTL::Texture*,  // linearImage
                         MTL::Texture*,  // rgbImage
                         Matrix3x3       // transform
                         >(mtlContext, "transformImage");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbImage->width, rgbImage->height, 1), linearImage.texture(),
           rgbImage->texture(), mtlTransform);
}

void denoiseImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                  const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, const gls::Vector<3>& var_a,
                  const gls::Vector<3>& var_b, const gls::Vector<3> thresholdMultipliers, float chromaBoost,
                  float gradientBoost, float gradientThreshold, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

    auto kernel = Kernel<MTL::Texture*,  // inputImage
                         MTL::Texture*,  // gradientImage
                         simd::float3,   // var_a
                         simd::float3,   // var_b
                         simd::float3,   // thresholdMultipliers
                         float,          // chromaBoost
                         float,          // gradientBoost
                         float,          // gradientThreshold
                         MTL::Texture*   // outputImage
                         >(mtlContext, "denoiseImage");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           inputImage.texture(), gradientImage.texture(),
           simd::float3 { var_a[0], var_a[1], var_a[2] },
           simd::float3 { var_b[0], var_b[1], var_b[2] },
           simd::float3 { thresholdMultipliers[0], thresholdMultipliers[1], thresholdMultipliers[2] },
           chromaBoost, gradientBoost, gradientThreshold, outputImage->texture());
}

template <typename T>
void resampleImage(MetalContext* mtlContext, const std::string& kernelName, const gls::mtl_image_2d<T>& inputImage,
                   gls::mtl_image_2d<T>* outputImage) {

    auto kernel = Kernel<MTL::Texture*,  // inputImage
                         MTL::Texture*>(mtlContext, kernelName);

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           inputImage.texture(), outputImage->texture());
}

template void resampleImage(MetalContext* mtlContext, const std::string& kernelName,
                            const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                            gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage);

template void resampleImage(MetalContext* mtlContext, const std::string& kernelName,
                            const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& inputImage,
                            gls::mtl_image_2d<gls::luma_alpha_pixel_float>* outputImage);

void subtractNoiseImage(MetalContext* mtlContext,
                        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage1,
                        const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImageDenoised1,
                        const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                        float luma_weight, float sharpening, const gls::Vector<2>& nlf,
                        gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

    auto kernel = Kernel<MTL::Texture*,  // inputImage
                         MTL::Texture*,  // inputImage1
                         MTL::Texture*,  // inputImageDenoised1
                         MTL::Texture*,  // gradientImage
                         float,          // luma_weight
                         float,          // sharpening
                         simd::float2,   // nlf
                         MTL::Texture*   // outputImage
                         >(mtlContext, "subtractNoiseImage");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           inputImage.texture(), inputImage1.texture(), inputImageDenoised1.texture(),
           gradientImage.texture(), luma_weight, sharpening, simd::float2 { nlf[0], nlf[1] },
           outputImage->texture());
}

void bayerToRawRGBA(MetalContext* mtlContext, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                    gls::mtl_image_2d<gls::rgba_pixel_float>* rgbaImage, BayerPattern bayerPattern) {
    assert(rawImage.width == 2 * rgbaImage->width && rawImage.height == 2 * rgbaImage->height);

    auto kernel = Kernel<MTL::Texture*,  // rawImage
                         MTL::Texture*,  // rgbaImage
                         int             // bayerPattern
                         >(mtlContext, "bayerToRawRGBA");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbaImage->width, rgbaImage->height, 1), rawImage.texture(),
           rgbaImage->texture(), bayerPattern);
}

void rawRGBAToBayer(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& rgbaImage,
                    gls::mtl_image_2d<gls::luma_pixel_float>* rawImage, BayerPattern bayerPattern) {
    assert(rawImage->width == 2 * rgbaImage.width && rawImage->height == 2 * rgbaImage.height);

    auto kernel = Kernel<MTL::Texture*,  // rgbaImage
                         MTL::Texture*,  // rawImage
                         int             // bayerPattern
                         >(mtlContext, "rawRGBAToBayer");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbaImage.width, rgbaImage.height, 1), rgbaImage.texture(),
           rawImage->texture(), bayerPattern);
}

void despeckleRawRGBAImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                           const gls::Vector<4> rawVariance, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

    auto kernel = Kernel<MTL::Texture*,  // inputImage
                         simd::float4,   // rawVariance
                         MTL::Texture*   // outputImage
                         >(mtlContext, "despeckleRawRGBAImage");

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1), inputImage.texture(),
           simd::float4 { rawVariance[0], rawVariance[1], rawVariance[2], rawVariance[3] }, outputImage->texture());
}

void despeckleImage(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                    const gls::Vector<3>& var_a, const gls::Vector<3>& var_b,
                    gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

    auto kernel = Kernel<MTL::Texture*,  // inputImage
                         simd::float3,   // var_a
                         simd::float3,   // var_b
                         MTL::Texture*   // outputImage
                         >(mtlContext, "despeckleLumaMedianChromaImage");

    simd::float3 cl_var_a = {var_a[0], var_a[1], var_a[2]};
    simd::float3 cl_var_b = {var_b[0], var_b[1], var_b[2]};

    // Schedule the kernel on the GPU
    kernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
           inputImage.texture(), cl_var_a, cl_var_b, outputImage->texture());
}

// Arrays order is LF, MF, HF
void localToneMappingMask(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                          const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage,
                          const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abImage,
                          const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abMeanImage,
                          const LTMParameters& ltmParameters, const gls::Vector<2>& nlf,
                          gls::mtl_image_2d<gls::luma_pixel_float>* outputImage) {
    for (int i = 0; i < 3; i++) {
        assert(guideImage[i]->width == abImage[i]->width && guideImage[i]->height == abImage[i]->height);
        assert(guideImage[i]->width == abMeanImage[i]->width && guideImage[i]->height == abMeanImage[i]->height);
    }

    // Bind the kernel parameters
    auto gfKernel = Kernel<MTL::Texture*,  // guideImage
                           MTL::Texture*,  // abImage
                           float           // eps
                           >(mtlContext, "GuidedFilterABImage");

    auto gfMeanKernel = Kernel<MTL::Texture*,  // inputImage
                               MTL::Texture*   // outputImage
                               >(mtlContext, "BoxFilterGFImage");

    auto ltmKernel = Kernel<MTL::Texture*,  // inputImage
                            MTL::Texture*,  // lfAbImage
                            MTL::Texture*,  // mfAbImage
                            MTL::Texture*,  // hfAbImage
                            MTL::Texture*,  // ltmMaskImage
                            LTMParameters,  // ltmParameters
                            simd::float2    // nlf
                            >(mtlContext, "localToneMappingMaskImage");

    // Schedule the kernel on the GPU
    for (int i = 0; i < 3; i++) {
        if (i == 0 || ltmParameters.detail[i] != 1) {
            gfKernel(mtlContext, /*gridSize=*/ MTL::Size(guideImage[i]->width, guideImage[i]->height, 1),
                     guideImage[i]->texture(), abImage[i]->texture(), ltmParameters.eps);

            gfMeanKernel(mtlContext, /*gridSize=*/ MTL::Size(abImage[i]->width, abImage[i]->height, 1),
                         abImage[i]->texture(), abMeanImage[i]->texture());
        }
    }

    ltmKernel(mtlContext, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1), inputImage.texture(),
              abMeanImage[0]->texture(), abMeanImage[1]->texture(), abMeanImage[2]->texture(),
              outputImage->texture(), ltmParameters, simd::float2 { nlf[0], nlf[1] });
}

void convertTosRGB(MetalContext* mtlContext, const gls::mtl_image_2d<gls::rgba_pixel_float>& linearImage,
                   const gls::mtl_image_2d<gls::luma_pixel_float>& ltmMaskImage,
                   gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, const DemosaicParameters& demosaicParameters) {
    const auto& transform = demosaicParameters.rgb_cam;

    struct Matrix3x3 {
        simd::float3 m[3];
    } mtlTransform = {{{transform[0][0], transform[0][1], transform[0][2]},
                       {transform[1][0], transform[1][1], transform[1][2]},
                       {transform[2][0], transform[2][1], transform[2][2]}}};

    // Bind the kernel parameters
    auto kernel = Kernel<MTL::Texture*,           // linearImage
                         MTL::Texture*,           // ltmMaskImage
                         MTL::Texture*,           // rgbImage
                         Matrix3x3,               // transform
                         RGBConversionParameters  // demosaicParameters
                         >(mtlContext, "convertTosRGB");

    kernel(mtlContext, /*gridSize=*/ MTL::Size(rgbImage->width, rgbImage->height, 1), linearImage.texture(),
           ltmMaskImage.texture(), rgbImage->texture(), mtlTransform, demosaicParameters.rgbConversionParameters);
}
