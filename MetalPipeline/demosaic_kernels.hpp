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

#ifndef demosaic_kernels_h
#define demosaic_kernels_h

#include <iostream>
#include <simd/simd.h>

#include "float16.hpp"

#include "gls_image.hpp"
#include "gls_mtl_image.hpp"
#include "gls_mtl.hpp"

#include "SimplexNoise.hpp"

struct scaleRawDataKernel {
    Kernel<MTL::Texture*,     // rawImage
           MTL::Texture*,     // scaledRawImage
           int,               // bayerPattern
           simd::half4,       // scaleMul
           half,              // blackLevel
           half               // lensShadingCorrection
    > kernel;

    scaleRawDataKernel(MetalContext* context) : kernel(context, "scaleRawData") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::luma_pixel_16>& rawImage,
                     gls::mtl_image_2d<gls::luma_pixel_float>* scaledRawImage, BayerPattern bayerPattern,
                     gls::Vector<4> scaleMul, float blackLevel, float lensShadingCorrection) {
        kernel(context, /*gridSize=*/ MTL::Size(scaledRawImage->width / 2, scaledRawImage->height / 2, 1),
               rawImage.texture(), scaledRawImage->texture(), bayerPattern,
               simd::half4 { (half) scaleMul[0], (half) scaleMul[1], (half) scaleMul[2], (half) scaleMul[3] },
               blackLevel, lensShadingCorrection);
    }
};

struct rawImageSobelKernel {
    Kernel<MTL::Texture*,  // rawImage
           MTL::Texture*   // gradientImage
    > kernel;

    rawImageSobelKernel(MetalContext* context) : kernel(context, "rawImageSobel") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* gradientImage) {
        kernel(context, /*gridSize=*/ MTL::Size(gradientImage->width, gradientImage->height, 1),
               rawImage.texture(),
               gradientImage->texture());
    }
};

struct gaussianBlurSobelImageKernel {
    Kernel<MTL::Texture*,  // rawImage
           MTL::Texture*,  // sobelImage
           int,            // samples1
           MTL::Buffer*,   // weights1
           int,            // samples2
           MTL::Buffer*,   // weights2
           simd::float2,   // rawVariance
           MTL::Texture*   // outputImage
    > kernel;

    gls::Buffer<std::array<float, 3>> weightsBuffer1, weightsBuffer2;

    gaussianBlurSobelImageKernel(MetalContext* context, float radius1, float radius2) :
    kernel(context, "sampledConvolutionSobel"),
    weightsBuffer1(context->device(), gaussianKernelBilinearWeights(radius1)),
    weightsBuffer2(context->device(), gaussianKernelBilinearWeights(radius2))
    { }

    void operator() (MetalContext* context,
                     const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& sobelImage,
                     std::array<float, 2> rawNoiseModel,
                     gls::mtl_image_2d<gls::luma_alpha_pixel_float>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               rawImage.texture(), sobelImage.texture(),
               (int) weightsBuffer1.size(), weightsBuffer1.buffer(),
               (int) weightsBuffer2.size(), weightsBuffer2.buffer(),
               simd::float2 { rawNoiseModel[0], rawNoiseModel[1] },
               outputImage->texture());
    }
};

struct hfNoiseTransferImageKernel {
    Kernel<MTL::Texture*,   // inputImage,
           MTL::Texture*,   // noisyImage,
           MTL::Texture*,   // outputImage
           int,             // samples
           MTL::Buffer*     // weights
    > kernel;

    gls::Buffer<std::array<float, 3>> weightsBuffer;

    hfNoiseTransferImageKernel(MetalContext* context, float radius) :
    kernel(context, "hfNoiseTransferImage"),
    weightsBuffer(context->device(), gaussianKernelBilinearWeights(radius))
    { }

    void operator() (MetalContext* context,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& noisyImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), noisyImage.texture(), outputImage->texture(),
               (int) weightsBuffer.size(), weightsBuffer.buffer());
    }
};

struct demosaicImageKernel {
    Kernel<MTL::Texture*,  // rawImage
           MTL::Texture*,  // gradientImage
           MTL::Texture*,  // greenImage
           int,            // bayerPattern
           simd::float2    // greenVariance
    > interpolateGreenKernel;

    Kernel<MTL::Texture*,  // rawImage
           MTL::Texture*,  // greenImage
           MTL::Texture*,  // gradientImage
           MTL::Texture*,  // rgbImage
           int,            // bayerPattern
           simd::float2,   // redVariance
           simd::float2    // blueVariance
    > interpolateRedBlueKernel;

    Kernel<MTL::Texture*,  // rgbImageIn
           MTL::Texture*,  // gradientImage
           MTL::Texture*,  // rgbImageOut
           int,            // bayerPattern
           simd::float2,   // redVariance
           simd::float2    // blueVariance
    > interpolateRedBlueAtGreenKernel;

    demosaicImageKernel(MetalContext* context) :
        interpolateGreenKernel(context, "interpolateGreen"),
        interpolateRedBlueKernel(context, "interpolateRedBlue"),
        interpolateRedBlueAtGreenKernel(context, "interpolateRedBlueAtGreen") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                     const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                     gls::mtl_image_2d<gls::luma_pixel_float>* greenImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImageTmp,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImageOut,
                     BayerPattern bayerPattern, std::array<gls::Vector<2>, 3> rawVariance) {
        assert(rawImage.size() == gradientImage.size());
        assert(rawImage.size() == greenImage->size());
        assert(rawImage.size() == rgbImageTmp->size());
        assert(rawImage.size() == rgbImageOut->size());

        const auto& redVariance = rawVariance[0];
        const auto& greenVariance = rawVariance[1];
        const auto& blueVariance = rawVariance[2];

        interpolateGreenKernel(context, /*gridSize=*/ MTL::Size(greenImage->width, greenImage->height, 1),
                               rawImage.texture(), gradientImage.texture(), greenImage->texture(),
                               bayerPattern, simd::float2 {greenVariance[0], greenVariance[1]});

        interpolateRedBlueKernel(context, /*gridSize=*/ MTL::Size(rgbImageTmp->width / 2, rgbImageTmp->height / 2, 1),
                                 rawImage.texture(), greenImage->texture(), gradientImage.texture(), rgbImageTmp->texture(), bayerPattern,
                                 simd::float2 {redVariance[0], redVariance[1]}, simd::float2 {blueVariance[0], blueVariance[1]});

        interpolateRedBlueAtGreenKernel(context, /*gridSize=*/ MTL::Size(rgbImageOut->width / 2, rgbImageOut->height / 2, 1),
                                        rgbImageTmp->texture(), gradientImage.texture(), rgbImageOut->texture(), bayerPattern,
                                        simd::float2 {redVariance[0], redVariance[1]}, simd::float2 {blueVariance[0], blueVariance[1]});
    }
};

struct bayerToRawRGBAKernel {
    Kernel<MTL::Texture*,  // rawImage
           MTL::Texture*,  // rgbaImage
           int             // bayerPattern
    > kernel;

    bayerToRawRGBAKernel(MetalContext* context) : kernel(context, "bayerToRawRGBA") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* rgbaImage, BayerPattern bayerPattern) {
        assert(rawImage.width == 2 * rgbaImage->width && rawImage.height == 2 * rgbaImage->height);

        kernel(context, /*gridSize=*/ MTL::Size(rgbaImage->width, rgbaImage->height, 1), rawImage.texture(),
               rgbaImage->texture(), bayerPattern);
    }
};

struct rawRGBAToBayerKernel {
    Kernel<MTL::Texture*,  // rgbaImage
           MTL::Texture*,  // rawImage
           int             // bayerPattern
    > kernel;

    rawRGBAToBayerKernel(MetalContext* context) : kernel(context, "rawRGBAToBayer") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& rgbaImage,
                     gls::mtl_image_2d<gls::luma_pixel_float>* rawImage, BayerPattern bayerPattern) {
        assert(rawImage->width == 2 * rgbaImage.width && rawImage->height == 2 * rgbaImage.height);

        kernel(context, /*gridSize=*/ MTL::Size(rgbaImage.width, rgbaImage.height, 1), rgbaImage.texture(),
               rawImage->texture(), bayerPattern);
    }
};

struct crossDenoiseRawRGBAImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           simd::half4,    // rawVariance
           half,          // strength
           MTL::Texture*   // outputImage
    > kernel;

    crossDenoiseRawRGBAImageKernel(MetalContext* context) : kernel(context, "crossDenoiseRawRGBAImage") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::Vector<4> rawVariance, float strength, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        std::cout << "Denoising RAW image with strength: " << strength << std::endl;
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(),
               simd::half4 { (half) rawVariance[0], (half) rawVariance[1], (half) rawVariance[2], (half) rawVariance[3] },
               (half) strength, outputImage->texture());
    }
};

struct despeckleRawRGBAImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           MTL::Texture*,  // gradientImage
           simd::float4,   // rawVariance
           MTL::Texture*   // outputImage
    > kernel;

    despeckleRawRGBAImageKernel(MetalContext* context) : kernel(context, "despeckleRawRGBAImage") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                     const gls::Vector<4> rawVariance, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), gradientImage.texture(),
               simd::float4 { rawVariance[0], rawVariance[1], rawVariance[2], rawVariance[3] },
               outputImage->texture());
    }
};

struct blendHighlightsImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           float,          // clip
           MTL::Texture*   // outputImage
    > kernel;

    blendHighlightsImageKernel(MetalContext* context) : kernel(context, "blendHighlightsImage") { }

    void operator() (MetalContext* context,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     float clip, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), clip, outputImage->texture());
    }
};

struct Matrix3x3 {
    simd::float3 m[3];
};

struct transformImageKernel {
    Kernel<MTL::Texture*,  // linearImage
           MTL::Texture*,  // rgbImage
           Matrix3x3       // transform
    > kernel;

    transformImageKernel(MetalContext* context) : kernel(context, "transformImage") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& linearImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage, const gls::Matrix<3, 3>& transform) {
        Matrix3x3 mtlTransform = {{{transform[0][0], transform[0][1], transform[0][2]},
                                   {transform[1][0], transform[1][1], transform[1][2]},
                                   {transform[2][0], transform[2][1], transform[2][2]}}};

        kernel(context, /*gridSize=*/ MTL::Size(rgbImage->width, rgbImage->height, 1), linearImage.texture(),
               rgbImage->texture(), mtlTransform);
    }

};

struct despeckleImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           simd::float3,   // var_a
           simd::float3,   // var_b
           MTL::Texture*   // outputImage
    > kernel;

    despeckleImageKernel(MetalContext* context) : kernel(context, "despeckleLumaMedianChromaImage") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::Vector<3>& var_a, const gls::Vector<3>& var_b,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        simd::float3 cl_var_a = {var_a[0], var_a[1], var_a[2]};
        simd::float3 cl_var_b = {var_b[0], var_b[1], var_b[2]};

        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), cl_var_a, cl_var_b, outputImage->texture());
    }
};

struct denoiseImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           MTL::Texture*,  // gradientImage
           simd::float3,   // var_a
           simd::float3,   // var_b
           simd::float3,   // thresholdMultipliers
           float,          // chromaBoost
           float,          // gradientBoost
           float,          // gradientThreshold
           MTL::Texture*   // outputImage
    > kernel;

    denoiseImageKernel(MetalContext* context) : kernel(context, "denoiseImage") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage, const gls::Vector<3>& var_a,
                     const gls::Vector<3>& var_b, const gls::Vector<3> thresholdMultipliers, float chromaBoost,
                     float gradientBoost, float gradientThreshold, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), gradientImage.texture(),
               simd::float3 { var_a[0], var_a[1], var_a[2] },
               simd::float3 { var_b[0], var_b[1], var_b[2] },
               simd::float3 { thresholdMultipliers[0], thresholdMultipliers[1], thresholdMultipliers[2] },
               chromaBoost, gradientBoost, gradientThreshold, outputImage->texture());
    }
};

struct blockMatchingDenoiseImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           MTL::Texture*,  // gradientImage
           MTL::Texture*,  // pcaImage
           simd::float3,   // var_a
           simd::float3,   // var_b
           simd::float3,   // thresholdMultipliers
           float,          // chromaBoost
           float,          // gradientBoost
           float,          // gradientThreshold
           float,          // lensShadingCorrection
           MTL::Texture*   // outputImage
    > kernel;

    blockMatchingDenoiseImageKernel(MetalContext* context) : kernel(context, "blockMatchingDenoiseImage") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                     const gls::mtl_image_2d<gls::pixel<uint32_t, 4>>& patchImage, const gls::Vector<3>& var_a,
                     const gls::Vector<3>& var_b, const gls::Vector<3> thresholdMultipliers,
                     float chromaBoost, float gradientBoost, float gradientThreshold, float lensShadingCorrection,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {

        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), gradientImage.texture(), patchImage.texture(),
               simd::float3 { var_a[0], var_a[1], var_a[2] },
               simd::float3 { var_b[0], var_b[1], var_b[2] },
               simd::float3 { thresholdMultipliers[0], thresholdMultipliers[1], thresholdMultipliers[2] },
               chromaBoost, gradientBoost, gradientThreshold, lensShadingCorrection, outputImage->texture());
    }
};

struct collectPatchesKernel {
    Kernel<MTL::Texture*, // inputImage
           MTL::Buffer*   // patches
    > kernel;

    collectPatchesKernel(MetalContext* context) : kernel(context, "collectPatches") { }

    void operator() (MetalContext* context,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     MTL::Buffer* patches) {

        kernel(context, /*gridSize=*/ MTL::Size(inputImage.width / 8, inputImage.height / 8, 1),
               inputImage.texture(), patches);
    }
};

struct pcaProjectionKernel {
    Kernel<MTL::Texture*,                         // inputImage
           std::array<std::array<half, 8>, 25>,   // pcaSpace
           MTL::Texture*                          // projectedImage
    > kernel;

    pcaProjectionKernel(MetalContext* context) : kernel(context, "pcaProjection") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const std::array<std::array<half, 8>, 25>& pcaSpace,
                     gls::mtl_image_2d<gls::pixel<uint32_t, 4>>* projectedImage) {

        kernel(context, /*gridSize=*/ MTL::Size(inputImage.width, inputImage.height, 1),
               inputImage.texture(), pcaSpace, projectedImage->texture());
    }
};

struct subtractNoiseImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           MTL::Texture*,  // inputImage1
           MTL::Texture*,  // inputImageDenoised1
           MTL::Texture*,  // gradientImage
           float,          // luma_weight
           float,          // sharpening
           simd::float2,   // nlf
           MTL::Texture*   // outputImage
    > kernel;

    subtractNoiseImageKernel(MetalContext* context) : kernel(context, "subtractNoiseImage") { }

    void operator() (MetalContext* context,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage1,
                     const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImageDenoised1,
                     const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                     float luma_weight, float sharpening, const gls::Vector<2>& nlf,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), inputImage1.texture(), inputImageDenoised1.texture(),
               gradientImage.texture(), luma_weight, sharpening, simd::float2 { nlf[0], nlf[1] },
               outputImage->texture());
    }

};

struct basicNoiseStatisticsKernel {
    Kernel<MTL::Texture*,   // inputImage
           MTL::Texture*    // statisticsImage
    > kernel;

    basicNoiseStatisticsKernel(MetalContext* context) : kernel(context, "basicNoiseStatistics") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* statisticsImage) {
        kernel(context, /*gridSize=*/ MTL::Size(statisticsImage->width, statisticsImage->height, 1),
               inputImage.texture(), statisticsImage->texture());
    }
};

struct basicRawNoiseStatisticsKernel {
    Kernel<MTL::Texture*,   // rawImage
           int,             // bayerPattern
           MTL::Texture*,   // meanImage
           MTL::Texture*    // varImage
    > kernel;

    basicRawNoiseStatisticsKernel(MetalContext* context) : kernel(context, "basicRawNoiseStatistics") { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::luma_pixel_float>& rawImage,
                     int bayerPattern,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* meanImage,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* varImage) {
        kernel(context, /*gridSize=*/ MTL::Size(meanImage->width, meanImage->height, 1),
               rawImage.texture(), bayerPattern, meanImage->texture(), varImage->texture());
    }
};

struct resampleImageKernel {
    Kernel<MTL::Texture*,   // inputImage
           MTL::Texture*    // outputImage
    > kernel;

    resampleImageKernel(MetalContext* context, const std::string& kernelName) : kernel(context, kernelName) { }

    template <typename T>
    void operator() (MetalContext* context, const gls::mtl_image_2d<T>& inputImage,
                     gls::mtl_image_2d<T>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), outputImage->texture());
    }
};

struct histogramImageKernel {
    Kernel<MTL::Texture*,  // inputImage
           MTL::Buffer*    // histogramBuffer
    > histogramImage;

    Kernel<MTL::Buffer*,  // histogramBuffer
           simd::uint2    // imageDimensions
    > histogramStatistics;

    struct histogram_data {
        std::array<uint32_t, 256> histogram;
        std::array<uint32_t, 8> bands;
        float black_level;
        float white_level;
        float shadows;
        float highlights;
        float mean;
        float median;
    };

    gls::Buffer<histogram_data> histogramBuffer;

    void reset() {
        bzero(histogramBuffer.data(), sizeof(histogram_data));
    }

    histogram_data* histogramData() const {
        return histogramBuffer.data();
    }

    MTL::Buffer* buffer() {
        return histogramBuffer.buffer();
    }

    histogramImageKernel(MetalContext* context) :
        histogramImage(context, "histogramImage"),
        histogramStatistics(context, "histogramStatistics"),
        histogramBuffer(context->device(), 1)
        { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage) {
        histogramImage(context, /*gridSize=*/ MTL::Size(inputImage.width, inputImage.height, 1),
               inputImage.texture(), histogramBuffer.buffer());
    }

    void statistics(MetalContext* context, const gls::size& imageDimensions) {
        histogramStatistics(context, /*gridSize=*/ MTL::Size(1, 1, 1),
                            histogramBuffer.buffer(),
                            simd::uint2 { (unsigned) imageDimensions.width, (unsigned) imageDimensions.height });
    }
};

struct localToneMappingMaskKernel {
    Kernel<MTL::Texture*,  // guideImage
           MTL::Texture*,  // abImage
           float           // eps
    > GuidedFilterABImage;

    Kernel<MTL::Texture*,  // inputImage
           MTL::Texture*   // outputImage
    > BoxFilterGFImage;

    Kernel<MTL::Texture*,  // inputImage
           MTL::Texture*,  // gradientImage
           MTL::Texture*,  // lfAbImage
           MTL::Texture*,  // mfAbImage
           MTL::Texture*,  // hfAbImage
           MTL::Texture*,  // ltmMaskImage
           LTMParameters,  // ltmParameters
           simd::float2,   // nlf
           MTL::Buffer*    // histogramBuffer
    > localToneMappingMaskImage;

    localToneMappingMaskKernel(MetalContext* context) :
        GuidedFilterABImage(context, "GuidedFilterABImage"),
        BoxFilterGFImage(context, "BoxFilterGFImage"),
        localToneMappingMaskImage(context, "localToneMappingMaskImage")
        { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::mtl_image_2d<gls::luma_alpha_pixel_float>& gradientImage,
                     const std::array<const gls::mtl_image_2d<gls::rgba_pixel_float>*, 3>& guideImage,
                     const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abImage,
                     const std::array<const gls::mtl_image_2d<gls::luma_alpha_pixel_float>*, 3>& abMeanImage,
                     const LTMParameters& ltmParameters, const gls::Vector<2>& nlf, MTL::Buffer* histogramBuffer,
                     gls::mtl_image_2d<gls::luma_pixel_float>* outputImage) {
        for (int i = 0; i < 3; i++) {
            assert(guideImage[i]->width == abImage[i]->width && guideImage[i]->height == abImage[i]->height);
            assert(guideImage[i]->width == abMeanImage[i]->width && guideImage[i]->height == abMeanImage[i]->height);
        }

        for (int i = 0; i < 3; i++) {
            GuidedFilterABImage(context, /*gridSize=*/ MTL::Size(guideImage[i]->width, guideImage[i]->height, 1),
                     guideImage[i]->texture(), abImage[i]->texture(), ltmParameters.eps);

            BoxFilterGFImage(context, /*gridSize=*/ MTL::Size(abImage[i]->width, abImage[i]->height, 1),
                         abImage[i]->texture(), abMeanImage[i]->texture());
        }

        localToneMappingMaskImage(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
                                  inputImage.texture(), gradientImage.texture(),
                                  abMeanImage[0]->texture(), abMeanImage[1]->texture(), abMeanImage[2]->texture(),
                                  outputImage->texture(), ltmParameters, simd::float2 { nlf[0], nlf[1] }, histogramBuffer);
    }
};

struct simplexNoiseKernel {
    Kernel<MTL::Texture*,   // inputImage
           MTL::Buffer*,    // permutation
           MTL::Buffer*,    // gradient
           simd::float2,    // lumaVariance
           MTL::Texture*    // outputImage
    > kernel;

    gls::Buffer<std::array<int, Noise2D::arraySize>> permBuffer;
    gls::Buffer<std::array<std::array<float, 2>, Noise2D::arraySize>> gradBuffer;

    void randomSeed(unsigned seed) {
        Noise2D::randomSeed(seed);
    }

    void initGradients() {
        auto& perm = *permBuffer.data();
        auto& grad = *gradBuffer.data();

        Noise2D::initGradients(&perm, &grad);
    }

    simplexNoiseKernel(MetalContext* context) :
        kernel(context, "simplex_noise"),
        permBuffer(context->device(), 1),
        gradBuffer(context->device(), 1)
    {
        initGradients();
    }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                     const gls::Vector<2>& luma_nlf, gls::mtl_image_2d<gls::rgba_pixel_float>* outputImage) {
        kernel(context, /*gridSize=*/ MTL::Size(outputImage->width, outputImage->height, 1),
               inputImage.texture(), permBuffer.buffer(), gradBuffer.buffer(), simd::float2 { luma_nlf[0], luma_nlf[1] },
               outputImage->texture());
    }
};

struct convertTosRGBKernel {
    Kernel<MTL::Texture*,           // linearImage
           MTL::Texture*,           // ltmMaskImage
           MTL::Texture*,           // rgbImage
           Matrix3x3,               // transform
           RGBConversionParameters, // demosaicParameters
           MTL::Buffer*,            // histogramBuffer
           simd::float2,            // lumaVariance
           MTL::Buffer*,            // noisePermutation
           MTL::Buffer*             // noiseGradient
    > kernel;

    gls::Buffer<std::array<int, Noise2D::arraySize>> permBuffer;
    gls::Buffer<std::array<std::array<float, 2>, Noise2D::arraySize>> gradBuffer;

    void randomSeed(unsigned seed) {
        Noise2D::randomSeed(seed);
    }

    void initGradients() {
        auto& perm = *permBuffer.data();
        auto& grad = *gradBuffer.data();

        Noise2D::initGradients(&perm, &grad);
    }

    convertTosRGBKernel(MetalContext* context) : kernel(context, "convertTosRGB"),
        permBuffer(context->device(), 1),
        gradBuffer(context->device(), 1)
        { }

    void operator() (MetalContext* context, const gls::mtl_image_2d<gls::rgba_pixel_float>& linearImage,
                     const gls::mtl_image_2d<gls::luma_pixel_float>& ltmMaskImage,
                     const DemosaicParameters& demosaicParameters, MTL::Buffer* histogramBuffer,
                     const gls::Vector<2>& luma_nlf,
                     gls::mtl_image_2d<gls::rgba_pixel_float>* rgbImage) {
        const auto& transform = demosaicParameters.rgb_cam;

        Matrix3x3 mtlTransform = {{{transform[0][0], transform[0][1], transform[0][2]},
                                   {transform[1][0], transform[1][1], transform[1][2]},
                                   {transform[2][0], transform[2][1], transform[2][2]}}};

        kernel(context, /*gridSize=*/ MTL::Size(rgbImage->width, rgbImage->height, 1), linearImage.texture(),
               ltmMaskImage.texture(), rgbImage->texture(), mtlTransform, demosaicParameters.rgbConversionParameters,
               histogramBuffer, simd::float2 { luma_nlf[0], luma_nlf[1] },permBuffer.buffer(), gradBuffer.buffer());
    }
};

#endif /* demosaic_kernels_h */
