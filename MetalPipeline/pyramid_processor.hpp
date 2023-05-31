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

#ifndef pyramid_processor_hpp
#define pyramid_processor_hpp

#include "demosaic.hpp"
#include "demosaic_kernels.hpp"

template <size_t levels>
struct PyramidProcessor {
    const int width, height;
    int fusedFrames;

    static constexpr bool usePatchSimiliarity = true;
    static constexpr int pcaPatchSize = 25;
    static constexpr int pcaSpaceSize = 8;

    denoiseImageKernel _denoiseImage;
    collectPatchesKernel _collectPatches;
    pcaProjectionKernel _pcaProjection;
    blockMatchingDenoiseImageKernel _blockMatchingDenoiseImage;
    subtractNoiseImageKernel _subtractNoiseImage;
    resampleImageKernel _resampleImage;
    resampleImageKernel _resampleGradientImage;
    basicNoiseStatisticsKernel _basicNoiseStatistics;
    hfNoiseTransferImageKernel _hfNoiseTransferImage;

    typedef gls::mtl_image_2d<gls::rgba_pixel_float> imageType;
    std::array<imageType::unique_ptr, levels - 1> imagePyramid;
    std::array<gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr, levels - 1> gradientPyramid;
    std::array<imageType::unique_ptr, levels> subtractedImagePyramid;
    std::array<imageType::unique_ptr, levels> denoisedImagePyramid;
    std::array<gls::mtl_image_2d<gls::pixel<uint32_t, 4>>::unique_ptr, levels> pcaImagePyramid;
    std::unique_ptr<gls::Buffer<std::array<float, pcaPatchSize>>> pcaPatches;
    std::array<std::array<float16_t, pcaSpaceSize>, pcaPatchSize> pcaSpace;
    gls::mtl_image_2d<gls::luma_pixel_float>::unique_ptr filteredLuma;

//    std::array<imageType::unique_ptr, levels> fusionImagePyramidA;
//    std::array<imageType::unique_ptr, levels> fusionImagePyramidB;
//    std::array<imageType::unique_ptr, levels> fusionReferenceImagePyramid;
//    std::array<gls::mtl_image_2d<gls::rgba_pixel_float>::unique_ptr, levels> fusionReferenceGradientPyramid;
//    std::array<imageType::unique_ptr, levels>* fusionBuffer[2];

    PyramidProcessor(MetalContext* context, int width, int height);

    imageType* denoise(MetalContext* context, std::array<DenoiseParameters, levels>* denoiseParameters,
                       const imageType& image, const gls::mtl_image_2d<gls::rgba_pixel_float>& gradientImage,
                       std::array<YCbCrNLF, levels>* nlfParameters, float exposure_multiplier, float lensShadingCorrection,
                       bool calibrateFromImage = false);

//    void fuseFrame(MetalContext* context, std::array<DenoiseParameters, levels>* denoiseParameters,
//                   const imageType& image, const gls::Matrix<3, 3>& homography,
//                   const gls::mtl_image_2d<gls::rgba_pixel_float>& gradientImage,
//                   std::array<YCbCrNLF, levels>* nlfParameters, float exposure_multiplier,
//                   bool calibrateFromImage = false);
//
//    imageType* getFusedImage(MetalContext* context);

    YCbCrNLF MeasureYCbCrNLF(MetalContext* context,
                             const gls::mtl_image_2d<gls::rgba_pixel_float>& inputImage,
                             gls::mtl_image_2d<gls::rgba_pixel_float> *noiseStats,
                             float exposure_multiplier);
};

#endif /* pyramid_processor_hpp */
